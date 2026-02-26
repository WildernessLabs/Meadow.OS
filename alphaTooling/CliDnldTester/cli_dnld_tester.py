#!/usr/bin/env python3
"""
Meadow File Downloader Tool
Repeatedly downloads a file to MCU board using Wilderness Labs CLI
and tracks success/failure statistics.

This version reads the CLI output and the syslog output to insure that the
downloaded file was 100% written to the Meadow file system
"""
import subprocess
import threading
import serial
import time
import datetime
import argparse
import sys
import re
from datetime import datetime
from enum import Enum

SERIAL_PORT_DEFINITION = "COM5"
ABS_PATH_TO_FILE_TO_USE_FOR_DOWNLOAD = "c:/HcomFiles/Text/TestFileGen.txt"
# SERIAL_PORT_DEFINITION = "/dev/ttyACM0"
# ABS_PATH_TO_FILE_TO_USE_FOR_DOWNLOAD = "/home/peter/wl/Meadow.OS/alphaTooling/CliDnldTester/TestFileGen.txt"

BRIEF_PAUSE_BETWEEN_DOWNLOADS = 0.1
POLL_SECONDS_BEFORE_FAILURE = 3.0
POLL_TOTAL_QUIT_DNLD_COUNT = POLL_SECONDS_BEFORE_FAILURE / BRIEF_PAUSE_BETWEEN_DOWNLOADS
MINIMUM_EXECUTION_TIME = 3.4

class exeRetCode(Enum):
    Success = 0
    Timeout = 1
    No100Exit = 2
    NoCLI = 3
    RunExecp = 4
    ExTooFast = 5
    PollErr = 6
    NoLogEOF = 7

class hcomDnldState(Enum):
    Unknown = 0
    Active = 1
    EndOfFile = 2
    Paused = 3
    TimeOut = 4

exeRetCodeString = ["Success", "Timeout", "No100Exit", "NoCLI", "RunExecp", "ExTooFast", "PollErr, NoEOF"]
exeRetCodeValue = [0, 0, 0, 0, 0, 0, 0, 0]

class MeadowFileDownloader:
    def __init__(self, file_path, fileSize):
        self.file_path = file_path
        self.fileSize = fileSize
        self.success_count = 0
        self.failure_count = 0
        self.iteration_count = 0
        self.download_completed = False
        self.current_percent = 0
        self.prev_percent = 0
        self.pollActiveCount = 0.0
        self.totDownloadTime = 0.0
        self.totExecutionTime = 0.01
        self.current_hcom_state = hcomDnldState.Unknown
        self.isThreadRunning = False
        self.isAppRunning = False
        self.runSerialPortThread = True
        self.CLIActive = False

    # syslog_reader is were a second thread runs monitoring syslog output
    # looking for the start and end of the download. This was necessary because
    # the current CLI generates it's own percent complete value for display
    # and always reports 100% even if all of the file is not written to the
    # file system
    def syslog_reader(self, serialPort):
        """Read the syslog serial output and monitor output"""
        
        syslog_line = ""

        self.isThreadRunning = True

        try:
            # Wait till CLI has started
            while not self.CLIActive:
                time.sleep(0.05)

            while self.runSerialPortThread:
                syslog_line = serialPort.readline().decode('utf_8').rstrip()
                print(f"\nSyslog:{syslog_line}")

                if not self.current_hcom_state == hcomDnldState.Active:
                    if "Meadow download begin" in syslog_line:
                        syslog_line = ""
                        print("Setting hcomDnldState to Active")
                        self.current_hcom_state = hcomDnldState.Active
                    continue

                # Only valid if not active
                elif self.current_hcom_state == hcomDnldState.Active:
                    if "EOF received from CLI" in syslog_line:
                        syslog_line = ""
                        print("Setting hcomDnldState to EndOfFile")
                        self.current_hcom_state = hcomDnldState.EndOfFile
                    continue
                else:
                    time.sleep(0.05)

        except Exception as e:
            print(f"Exception: Syslog open or read stopped: {e}", flush=True)
            self.isAppRunning = False


    def run_cli_command(self):
        """Execute the meadow CLI command and monitor output"""

        # Reset a few variables
        self.download_completed = False
        self.current_percent = 0
        cli_exit_count = 0

        try:
            # Assemble the meadow file write command
            cmd = ["meadow", "file", "write", "-f", self.file_path]

            # Start the process
            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,   # stderr added to stdout
                universal_newlines=True,
                bufsize=1
            )

            # Monitor output in real-time
            while True:
                time.sleep(BRIEF_PAUSE_BETWEEN_DOWNLOADS)    # 0.1 seconds
                self.pollActiveCount = self.pollActiveCount + 1

                # If it's took too long quit
                if self.pollActiveCount > POLL_TOTAL_QUIT_DNLD_COUNT:
                    print(f"TIMEOUT: No output for > {POLL_SECONDS_BEFORE_FAILURE} seconds - marking as failure", flush=True)
                    process.terminate()
                    return False

                # Is there text from CLI?
                output = process.stdout.readline()
                if output != "":
                    self.pollActiveCount = 0
                    self.process_if_percent(output.strip())

                # Got % value return code CLI exited, check return code. There
                # is a problem, sometimes the "100%"" comes after the
                # return code signifies CLI has exited. The following code
                # compensates for this edge case.
                if self.download_completed: # 100% complete
                    return_code = process.poll()
                    if return_code == 0:    # Clean exit?
                        return exeRetCode.Success # Success exit
                    else:
                        # This can happen, but rare
                        continue            # 100% but CLI running, recheck
                else:
                    # Not 100% yet
                    return_code = process.poll()
                    if return_code == None:
                        continue            # Normal loop

                    # CLI has stopped, but 100% from CLI has not been seen
                    cli_exit_count = cli_exit_count + 1
                    if cli_exit_count == 1:
                        continue        # Try again to detect 100%

                    if cli_exit_count < 5:
                        print(f"CLI exit count:{cli_exit_count}, Percent:{self.current_percent}", end="\n", flush=True)
                        continue        # Keep trying

                    print(f"CLI exit count:{cli_exit_count}, Percent:{self.current_percent}", end="\n", flush=True)
                    return exeRetCode.No100Exit     # Time to give up

        except FileNotFoundError:
            print("ERROR: 'meadow' CLI not found. Please ensure Wilderness Labs CLI is installed and in PATH.", flush=True)
            return exeRetCode.NoCLI
        
        except Exception as e:
            print(f"ERROR: Exception during CLI execution: {e}", flush=True)
            return exeRetCode.RunExecp

    def process_if_percent(self, output):
        """Parse CLI output to extract download progress"""
        # Look for percentage indicators
        percent_match = re.search(r'(\d+)%', output)
        if percent_match:
            # We found a '%' sign
            percent = int(percent_match.group(1))
            self.current_percent = percent
            
            if percent == 100:
                print(f"CLI Percent reported: {percent}%", end="\r", flush=True)
                self.download_completed = True
                return True     # Found '100%'
            else:
                # Only show if different that previous
                if percent != self.prev_percent:
                    self.prev_percent = percent
                    print(f"CLI Percent reported: {percent}%", end="\r", flush=True)
                return True     # Found '%'
        else:
            return False

    def display_stats(self):
        """Display current statistics"""
        total = self.success_count + self.failure_count
        success_rate = (self.success_count / total * 100) if total > 0 else 0
        downloadRate = (self.fileSize / self.totDownloadTime) / 1024

        print(f"DOWNLOAD STATISTICS - {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"File: {self.file_path}, size: {self.fileSize/1024:.1f} KB")
        print(f"Total Iteration: {self.iteration_count}")
        print(f"Total Successes: {self.success_count}")
        print(f"Total Failures : {self.failure_count}")
        print(f"Success Percent: {success_rate:.1f}%")
        print(f"Download KB/Sec: {downloadRate:.1f} KB/Sec")
        print(f"Total Download Time: {self.totDownloadTime:.2f} seconds")

        retCodeMaxOffset = len(exeRetCodeValue) - 1
        for offset in range(0, retCodeMaxOffset):
            retVal = exeRetCodeValue[offset]
            if offset < retCodeMaxOffset:
                print (f"{exeRetCodeString[offset]}:{retVal},", end=" ")
            else:
                print (f"{exeRetCodeString[offset]}:{retVal}")

    def display_final_stats(self):
        """Display summary statistics at the end"""

        total = self.success_count + self.failure_count
        success_rate = (self.success_count / total * 100) if total > 0 else 0
        averageDownloadRate = ((self.fileSize * self.iteration_count) / self.totExecutionTime) / 1024
        exeTimeMin, exeTimeSec = divmod(self.totExecutionTime, 60)
        exeTimeHour, exeTimeMin = divmod(exeTimeMin, 60)

        print("="*60)
        print(f"FINAL STATISTICS - {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"File: {self.file_path}, size: {self.fileSize/1024:.1f} KB")
        print(f"Iterations: {self.iteration_count}")
        print(f"Successes: {self.success_count}")
        print(f"Failures:  {self.failure_count}")
        print(f"Success Rate: {success_rate:.1f}%")
        print(f"Average KB/Second: {averageDownloadRate:.1f} KB/Sec")
        print(f"Total Download Time: {exeTimeHour:1.0f}:{exeTimeMin:02.0f}:{exeTimeSec:02.2f}")

        print("="*60, flush=True)

    def run_continuous(self):
        """Main loop to continuously download the file"""
        print(f"Starting continuous download of: {self.file_path}")
        print("Press Ctrl+C to stop...", flush=True)

        self.isAppRunning = True

        try:
            # One time syslog read initialization done here
            # Open serial port and create a thread to reading syslog
            syslog_serial_port = serial.Serial(SERIAL_PORT_DEFINITION, 115200, timeout=1000)

            if not syslog_serial_port.is_open:
                print("Serial Port failed to open, bye")
                exit()

            syslog_reader_thread = threading.Thread(
                target=self.syslog_reader,
                args=(syslog_serial_port,))
            
            syslog_reader_thread.start()

            # Wait for thread to start
            while not self.isThreadRunning:
                time.sleep(0.1)

            # This begins the run loop
            while self.isAppRunning:
                self.iteration_count += 1
                print("\n" + "="*20, end=" ")
                print(f"Starting iteration {self.iteration_count} ", end=" ")
                print("="*20)

                # Run CLI download
                startDateTime = time.time()
                self.CLIActive = True
                # Waits for CLI to exit
                cliReturn = self.run_cli_command()
                self.CLIActive = False

                endDateTime = time.time()
                currentExecutionTime = endDateTime - startDateTime
                self.totDownloadTime = self.totDownloadTime + currentExecutionTime
                self.totExecutionTime = self.totExecutionTime + currentExecutionTime

                if cliReturn == exeRetCode.Success:
                    # Was the execution time large enough to be valid?
                    if currentExecutionTime < MINIMUM_EXECUTION_TIME:
                        cliReturn = exeRetCode.ExTooFast # Executed too fast

                if cliReturn == exeRetCode.Success:

                    # Wait for syslog to indicate EOF, but don't wait forever
                    sleepWaitCnt = 0
                    while (self.current_hcom_state != hcomDnldState.EndOfFile):
                        sleepWaitCnt = sleepWaitCnt + 1
                        if sleepWaitCnt < 20:     # Wait up to 1 seconds for EOF
                            time.sleep(50 / 1000)
                            continue

                    # Waited too long or got EOF
                    if self.current_hcom_state != hcomDnldState.EndOfFile:
                        print(f"\nWaited {sleepWaitCnt} for hcomDnldState.EndOfFile but, gave up")
                        self.current_hcom_state = hcomDnldState.TimeOut
                        cliReturn = exeRetCode.NoLogEOF # Never saw syslog EOF
                    else:
                        print("\nSyslog indicates EOF")

                # All possible CLI returns have been determined, tally the sums
                exeRetCodeValue[cliReturn.value] = exeRetCodeValue[cliReturn.value] + 1

                if cliReturn == exeRetCode.Success:
                    self.success_count += 1
                    print(f"\n✓ Iteration {self.iteration_count} SUCCESS")
                else:
                    self.failure_count += 1
                    print(f"\n✗ Iteration {self.iteration_count} FAILURE")
            
                # Display updated statistics
                self.display_stats()

                # IS THIS NEEDED?
                # Pause before next download
                # print("Setting hcomDnldState to Paused")
                self.current_hcom_state = hcomDnldState.Paused
                time.sleep(0.3)

        # Won't work while debugging
        except KeyboardInterrupt:
            self.runSerialPortThread = False
            print("\nStopping tool (Ctrl+C received)")
            self.totDownloadTime = 0.0
            self.iteration_count = self.iteration_count - 1
            self.isAppRunning = False
            syslog_reader_thread.stop()
            syslog_reader_thread.join()

        except Exception as e:
            self.runSerialPortThread = False
            print(f"\nStopping unexpected exception: {e}")
            self.totDownloadTime = 0.0
            self.iteration_count = self.iteration_count - 1
            self.isAppRunning = False
            syslog_reader_thread.stop()
            syslog_reader_thread.join()

        print("\nFinal Statistics:")
        self.display_final_stats()


def main():
    parser = argparse.ArgumentParser(
        description="Meadow File Downloader - Continuously download files to MCU board",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python meadow_file_downloader.py -f C:\\MyFiles\\firmware.bin
  python meadow_file_downloader.py -f /path/to/file.hex
        """
    )

    # parser.add_argument(
    #     '-f', '--file',
    #     required=True,
    #     help='Path to the file to download to the MCU board'
    # )

    # print("Parsing command tail")

    # args = parser.parse_args()
    
    # # Validate file path
    import os
    # if not os.path.exists(args.file):
    #     print(f"ERROR: File does not exist: {args.file}")
    #     sys.exit(1)

    # fileSize = os.path.getsize(args.file)

    fileSize = os.path.getsize(ABS_PATH_TO_FILE_TO_USE_FOR_DOWNLOAD)

    # Create and run the downloader
    downloader = MeadowFileDownloader(ABS_PATH_TO_FILE_TO_USE_FOR_DOWNLOAD, fileSize)
    downloader.run_continuous()

if __name__ == "__main__":
    main()
