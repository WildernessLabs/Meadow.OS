#!/usr/bin/env python3
"""
Meadow File Downloader Test Tool
Repeatedly downloads a file to Meadow board using the Wilderness Labs CLI
and tracks success/failure statistics.

This version read the syslog output to insure that the downloaded
file was 100% written to the Meadow file system. This was necessary because
the current CLI outputs '100%' even when download fails.

In hcom_file_dnld_stm32f7.c set HCOM_FILE_DNLD_SYSLOG_FOR_TESTING to 1 and
rebuild and flash to a Meadow board

TARGET_ITERATION_COUNT should be set to the number if iterations desired and
SYSLOG_SERIAL_PORT_DEFINITION set to the syslog serial port, which is usually
the ST-Link virtual port

Start from command prompt
For Windows> python C:\WlApps\CliDnldTester\cli_dnld_tester.py
"""

import subprocess
import threading
import serial
import time
import datetime
import argparse
import os
import sys
import re
from datetime import datetime
from enum import Enum

TARGET_ITERATION_COUNT = 10
SYSLOG_SERIAL_PORT_DEFINITION = "COM5"
# SYSLOG_SERIAL_PORT_DEFINITION = "/dev/ttyACM0"

ABS_PATH_TO_FILE_TO_DOWNLOAD = "c:/HcomFiles/Write/Text/TestFileGen.txt"    # 200kb file
# ABS_PATH_TO_FILE_TO_DOWNLOAD = "c:/HcomFiles/Write/Text/TestFileOne.txt"    # 1 byte file
# ABS_PATH_TO_FILE_TO_DOWNLOAD = "c:/HcomFiles/Write/Text/TestFileKJV.txt"    # 4.4Mb file

POLL_SECONDS_BEFORE_FAILURE = 3.0 # Extra time wait after CLI stops
MINIMUM_EXECUTION_TIME = 1.5    # It must take at least this long to execute

class exeRetCode(Enum):
    Success = 0
    Timeout = 1
    NoCLI = 2
    RunExecp = 3
    ExTooFast = 4
    CliError = 5
    NoLogEOF = 6

class hcomDnldState(Enum):
    Unknown = 0
    DnldActive = 1        # Download in progress (0-100%)
    DnldSuccess = 2 
    DnldFailure = 3
    DnldTimeOut = 4       # Took too long to respond

exeRetCodeString = ["Success", "Timeout", "NoCLI", "RunExecp", "ExTooFast", "CliError", "NoEOF"]
exeRetCodeValue = [0, 0, 0, 0, 0, 0, 0]

class MeadowFileDownloader:
    def __init__(self, file_path, fileSize):
        self.file_path = file_path
        self.fileSize = fileSize
        self.success_count = 0
        self.failure_count = 0
        self.iteration_count = 0
        self.current_percent = 0
        self.prev_percent = 0
        self.iterationExecTime = 0.0
        self.totDownloadTime = 0.0
        self.totExecutionTime = 0.01    # This 0.01 prevented divide by 0 exceptions
        self.isThreadRunning = False
        self.isAppRunning = False

    # syslog_reader is were a second thread runs monitoring syslog output.
    # Three syslog message need to be generated for testing, the start, the
    # end and progress. This was necessary because the current CLI generates
    # it's own percentage messages and reports 100% even if the download
    # fails.
    def syslog_reader(self, serialPort, dnldFinishedEvent, syslogPauseEvent):
        """Read the syslog serial output and monitor output"""
        
        current_hcom_state = hcomDnldState.Unknown
        self.isThreadRunning = True
        syslog_line = ""

        try:
            while self.isAppRunning:
                # Blocking read
                syslog_line = serialPort.readline().decode('utf_8').rstrip()
                # print(f"\nSyslog:{syslog_line}")

                # Trash any syslog message generated when not doing download
                # If syslogPauseEvent is not set spin here
                if not syslogPauseEvent.is_set():
                    continue

                if not current_hcom_state == hcomDnldState.DnldActive:
                    if "Meadow download begin" in syslog_line:
                        syslog_line = ""
                        current_hcom_state = hcomDnldState.DnldActive
                        continue
                else:
                    # The file has been downloaded either successfully or not
                    if "Download success" in syslog_line:
                        current_hcom_state = hcomDnldState.DnldSuccess
                        syslogPauseEvent.clear() # We're done with this download
                        dnldFinishedEvent.set()  # Let CLI monitoring check our results
                        syslog_line = ""
                        continue

                    elif "Download failure" in syslog_line:
                        current_hcom_state = hcomDnldState.DnldFailure
                        syslogPauseEvent.clear()
                        dnldFinishedEvent.set()  # Let CLI monitoring check our results
                        syslog_line = ""
                        continue

                    else:
                        # Perhaps it contains '%' download progress information we'll 
                        # read and display it to entertain the user
                        percent_match = re.search(r'(\d+)%', syslog_line) # Look for %
                        if percent_match:
                            # We found a '%' sign, find preceeding numeric value
                            percent = int(percent_match.group(1))
                            self.current_percent = percent

                            if percent != self.prev_percent:
                                # percent has changed
                                self.prev_percent = percent
                                if percent == 100:
                                    print(f"Percent downloaded:{percent}%", flush=True)
                                else:
                                    print(f"Percent downloaded:{percent}%", end="\r", flush=True)
                    
                    syslog_line = ""        # Ignore the line
                    continue

        except Exception as e:
            print(f"Exception: Syslog open or read: {e}", flush=True)
            self.isAppRunning = False

        self.isThreadRunning = False


    # This function will Launch, Monitor and Wait for the CLI to exit
    def run_cli_command(self):
        """Execute the meadow CLI command and monitor output"""

        try:
            # Assemble the meadow file write command
            cmd = ["meadow", "file", "write", "-f", self.file_path]

            # Start CLI via command
            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,   # stderr added to stdout
                universal_newlines=True,
                bufsize=1
            )

            # Start CLI and wait for it to terminate
            # This code ignores the text messages generated by CLI
            while True:
                # Look at the CLI return code (this is available when
                # CLI terminates). It can have 1 of 3 states:
                # 'None' (CLI still running)
                # '0' (Terminated with no errors)
                #  a numeric value (some type of error).
                return_code = process.poll()
                if(return_code == None):    # CLI still running
                    continue
                elif(return_code == 0):     # CLI exited with no errors
                    # print(f"return_code:{return_code} CLI Success exit")
                    return exeRetCode.Success
                else:                       # CLI exited with an error
                    print(f"Error: return_code:{return_code} CLI error exit")
                    return exeRetCode.CliError

        except FileNotFoundError:
            print("ERROR: 'meadow' CLI not found. Please ensure Wilderness Labs CLI is installed and in PATH.", flush=True)
            return exeRetCode.NoCLI
        
        except Exception as e:
            print(f"ERROR: Exception during CLI execution: {e}", flush=True)
            return exeRetCode.RunExecp


    def run_continuous(self):
        """Main loop to continuously download the file"""

        print(f"Starting continuous download of: {self.file_path}")
        print("Press Ctrl+C to stop...", flush=True)

        syslog_reader_thread = threading
        self.iterationExecTime = 0.0
        self.isAppRunning = True

        # 'set' releases thread from wait()
        # This event is set when HCOM indicates it has finished the download
        dnldFinishedEvent = threading.Event()
        # This event allows commanding syslog reader to pause till needed
        syslogPauseEvent = threading.Event()

        dnldFinishedEvent.clear()

        try:
            # One time syslog serial read initialization done here
            syslog_serial_port = serial.Serial(SYSLOG_SERIAL_PORT_DEFINITION,
                                               115200, timeout=1000)
            if not syslog_serial_port.is_open:
                print("Serial Port failed to open")
                return

            syslog_reader_thread = threading.Thread(
                target=self.syslog_reader,
                args=(syslog_serial_port, dnldFinishedEvent, syslogPauseEvent))
            
            # Start the syslog reader thread
            syslog_reader_thread.start()

        except Exception as e:
            print(f"Initialization exception: {e}")
            self.totDownloadTime = 0.0
            self.iteration_count = 0
            self.isAppRunning = False

        try:
            # Wait for syslog monitor thread to start
            while not self.isThreadRunning:
                time.sleep(0.1)

            # This begins the run loop
            while self.isAppRunning:
                self.iteration_count += 1
                if self.iteration_count > TARGET_ITERATION_COUNT:
                    return

                print("\n" + "="*20, end=" ")
                print(f"Iteration {self.iteration_count} ", end=" ")
                print("="*20)

                # Prepare for download command processing
                syslogPauseEvent.set()          # Allow syslog reading
                dnldFinishedEvent.clear()       # Will be 'set' when download ends

                startDateTime = time.time()
                # Launch CLI to do download
                cliDnldReturn = self.run_cli_command()  # Set initial cliDnldReturn value

                endDateTime = time.time()
                
                self.iterationExecTime = endDateTime - startDateTime
                self.totDownloadTime = self.totDownloadTime + self.iterationExecTime
                self.totExecutionTime = self.totExecutionTime + self.iterationExecTime

                # If the syslog messages indicate that the download is
                # finished this wait() may block. But, if it does it will
                # block for a short time because sometime extra time is
                # needed for syslog to return the final syslog message.

                # Wait for Hcom to finish if needed
                dnldFinishedEvent.wait(POLL_SECONDS_BEFORE_FAILURE)
                # Did we wait too long?
                if not dnldFinishedEvent.is_set():
                    # Not 'set' means the wait timedout
                    cliDnldReturn = exeRetCode.Timeout

                if cliDnldReturn == exeRetCode.Success:
                    # Was the execution time large enough to be valid?
                    if self.iterationExecTime < MINIMUM_EXECUTION_TIME:
                        cliDnldReturn = exeRetCode.ExTooFast

                if self.current_percent != 100:
                    cliDnldReturn = exeRetCode.NoLogEOF # Success/Failure msg

                # All possible result values have been determined, add it to the totals
                exeRetCodeValue[cliDnldReturn.value] = exeRetCodeValue[cliDnldReturn.value] + 1

                if cliDnldReturn == exeRetCode.Success:
                    self.success_count += 1
                    print(f"✓ Iteration {self.iteration_count} SUCCESS")
                else:
                    self.failure_count += 1
                    print(f"✗ Iteration {self.iteration_count} FAILURE")

                # Display updated statistics
                self.display_stats()

        # Won't work while debugging
        except KeyboardInterrupt:
            print("\nStopping tool (Ctrl+C Detected)")
            self.totDownloadTime = 0.0
            self.iteration_count = self.iteration_count - 1
            self.isAppRunning = False

        except Exception as e:
            print(f"\nStopping unexpected exception: {e}")
            self.totDownloadTime = 0.0
            self.iteration_count = self.iteration_count - 1
            self.isAppRunning = False
                
        print("\nFinal Statistics:")
        self.display_final_stats()


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
        print(f"Download time  : {self.iterationExecTime:.2f} seconds")
        print(f"Aver KB/Second : {downloadRate:.1f} KB/Sec")
        print(f"Total Download Time: {self.totDownloadTime:.2f} seconds")

        retCodeMaxOffset = len(exeRetCodeValue)
        for offset in range(0, retCodeMaxOffset):
            retVal = exeRetCodeValue[offset]
            if offset < retCodeMaxOffset - 1:
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

def main():
    parser = argparse.ArgumentParser(
        description="Meadow File Downloader - Continuously download files to Meadow",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python meadow_file_downloader.py -f C:\\MyFiles\\firmware.bin
  python meadow_file_downloader.py -f /path/to/file.hex
        """
    )

    #  To supply the file to download from the command line
    #  Use the following code:

    # parser.add_argument(
    #     '-f', '--file',
    #     required=True,
    #     help='Path to the file to download to the MCU board'
    # )

    # print("Parsing command tail")

    # args = parser.parse_args()
    
    # # Validate file path
    # if not os.path.exists(args.file):
    #     print(f"ERROR: File does not exist: {args.file}")
    #     sys.exit(1)

    # fileSize = os.path.getsize(args.file)

    fileSize = os.path.getsize(ABS_PATH_TO_FILE_TO_DOWNLOAD)

    # Create and run the downloader
    downloader = MeadowFileDownloader(ABS_PATH_TO_FILE_TO_DOWNLOAD, fileSize)
    downloader.run_continuous()

if __name__ == "__main__":
    main()
