#!/usr/bin/env python3

"""
Meadow File Downloader Test Tool
Repeatedly downloads a file to Meadow board using the Wilderness Labs CLI
and tracks success/failure statistics.

This version read the syslog output to insure that the downloaded
file was 100% written to the Meadow file system. This was necessary because
the current CLI outputs '100%' even when download fails.

To use tool the following must be done:
1. In hcom_file_dnld_stm32f7.c set HCOM_FILE_DNLD_SYSLOG_FOR_TESTING to 1 and
    rebuild and flash to a Meadow board.
2. TARGET_ITERATION_COUNT set to the number if iterations desired.
3. SYSLOG_SERIAL_PORT_DEFINITION set to the syslog serial port (usually
   the ST-Link virtual port).
4. Execute the following commands to insure syslog output on UART
   'meadow uart trace enable'  - Sent trace messages to UART
   'meadow trace level 3'      - Trace level 3 will send the messages
   'meadow trace disable'      - Don't send trace to CLI
5. Start from command prompt
   (for Windows)> python "C:/WlApps/CliDnldTester/cli_dnld_tester.py"

Trouble shooting:
If tool doesn't work, use Putty or another serial output application and
  assign it to the COM Port defined by SYSLOG_SERIAL_PORT_DEFINITION instead
  of this tool. You should see the syslog output.
"""

import subprocess
import threading
import serial
import time
import datetime
import argparse
import os
import re
from datetime import datetime
from enum import Enum

# ---> Set relevant values <---
TARGET_ITERATION_COUNT = 10000

SYSLOG_SERIAL_PORT_DEFINITION = "COM5"
# SYSLOG_SERIAL_PORT_DEFINITION = "/dev/ttyACM0"
# ABS_PATH_TO_FILE_TO_DOWNLOAD = "c:/HcomFiles/Write/Binary/mscorlib.dll"     # 4.5MB file
ABS_PATH_TO_FILE_TO_DOWNLOAD = "c:/HcomFiles/Write/Text/TestFileGen.txt"  # 200kb file
# ABS_PATH_TO_FILE_TO_DOWNLOAD = "c:/HcomFiles/Write/Text/TestFileOne.txt"  # 1 byte file
# ABS_PATH_TO_FILE_TO_DOWNLOAD = "c:/HcomFiles/Write/Text/TestFileKJV.txt"  # 4.4Mb file

POLL_SECONDS_BEFORE_FAILURE = 3.0 # Extra time to wait for syslog end after CLI stops
MINIMUM_EXECUTION_TIME = 1.5    # It must take at least this long to execute

# Syslog monitoring can indicate one of these
class hcomDnldState(Enum):
    DnldWaitStart = 0
    DnldActive = 1        # Download in progress (0-100%)
    DnldSuccess = 2 
    DnldFailure = 3

# Executing CLI can indicate one of these
class exeCliRet(Enum):
    exeCliSuccess  = 1
    exeCliNotFound = 2
    exeCliRunErr   = 3
    exeCliRunExecp = 4

# These includes all detectable errors
class reportableCodes(Enum):
    Success = 0
    NoCLIFound = 2
    CliRunError = 3
    CliRunExecp = 4
    SyslogTimeout = 5
    NoSyslogEOF = 6
    ExTooFast = 7
    HcomError = 8
    SyncError = 9

exeRetCodeString = ["Success", "NoCLIFound", "CliRunError", "CliRunExecp", "SyslogTimeout", "NoSyslogEOF", "ExTooFast", "HcomError", "SyncError"]
exeRetCodeValue = [0, 0, 0, 0, 0, 0, 0, 0, 0]

class MeadowFileDownloader:
    def __init__(self, file_path, fileSize):
        self.file_path = file_path
        self.fileSize = fileSize
        self.success_count = 0
        self.failure_count = 0
        self.iteration_count = 0
        self.prev_percent = 0
        self.iterationExecTime = 0.0
        self.totDownloadTime = 0.0
        self.isThreadRunning = False
        self.isAppRunning = False
        self.current_dnld_state = hcomDnldState.DnldWaitStart
        self.reached100Percent = False

    # 'syslog_reader' is were a second thread runs monitoring syslog output.
    # Three syslog message are used, 'start', 'end result' and 'percent
    # progress'.
    # Since CLI generates it's own percentage messages and reports 100% even
    # if the download fails. It became a requirement to use syslog to insure
    # no false positives were generated.
    def syslog_reader(self, serialPort, dnldFinishedEvent, syslogPauseEvent):
        """Read the syslog serial output and monitor output"""
        
        self.isThreadRunning = True
              
        try:
            while self.isAppRunning:
                # Wait for syslogPauseEvent indicating okay to read syslog
                if not syslogPauseEvent.wait(timeout=0.1):
                    continue

                # Blocking read
                syslog_line = serialPort.readline().decode('utf_8').rstrip()
                # print(f"\nSyslog:{syslog_line}")

                if self.current_dnld_state == hcomDnldState.DnldWaitStart:
                    # Waiting for download to begin
                    if "Meadow download begin" in syslog_line:
                        self.current_dnld_state = hcomDnldState.DnldActive

                elif self.current_dnld_state == hcomDnldState.DnldActive:
                    # Download is active - check for completion or progress
                    if "Download success" in syslog_line:
                        self.current_dnld_state = hcomDnldState.DnldSuccess
                        syslogPauseEvent.clear()
                        dnldFinishedEvent.set()
                    elif "Download failure" in syslog_line:
                        self.current_dnld_state = hcomDnldState.DnldFailure
                        syslogPauseEvent.clear()
                        dnldFinishedEvent.set()
                    else:
                        # Check for download progress percentage
                        if '%' in syslog_line:
                            percent_match = re.search(r'(\d+)%', syslog_line)
                            if percent_match:
                                percent = int(percent_match.group(1))
                                if percent != self.prev_percent:
                                    self.prev_percent = percent
                                    # If 100% download set state to temporary state so that
                                    # if CLI end-of-file never reached we know why. This is
                                    # overwritten when the success message comes from CLI
                                    if percent == 100:
                                        self.reached100Percent = True
                                    # Print with carriage return except for 100%
                                    end_char = "\n" if percent == 100 else "\r"
                                    print(f"Percent downloaded:{percent}%", end=end_char, flush=True)

        except Exception as e:
            print(f"Exception: Syslog open or read: {e}", flush=True)
            self.isAppRunning = False

        self.isThreadRunning = False


    # This function will launch CLI and wait for the CLI to exit
    def run_cli_command(self):
        """Execute the meadow CLI command and monitor output"""

        try:
            # Assemble the meadow file write command
            cmd = ["meadow", "file", "write", "-f", self.file_path]

            # Start CLI, discarding output since we can't use CLI output
            process = subprocess.Popen(
                cmd,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )

            # Block until process terminates
            return_code = process.wait()
            
            if return_code < 0:
                print(f"Error: return_code:{return_code} CLI error exit")
                return exeCliRet.exeCliRunErr
            else:
                return exeCliRet.exeCliSuccess

        except FileNotFoundError:
            print("ERROR: 'meadow' CLI not found. Ensure Wilderness Labs CLI is installed and in PATH.", flush=True)
            return exeCliRet.exeCliNotFound

        except Exception as e:
            print(f"ERROR: Exception during CLI execution: {e}", flush=True)
            return exeCliRet.exeCliRunExecp


    def run_continuous(self):
        """Main loop to continuously download the file"""

        print(f"Starting continuous download of: {self.file_path}")
        print("Press Ctrl+C to stop...", flush=True)

        self.iterationExecTime = 0.0
        self.isAppRunning = True

        # Threading events
        dnldFinishedEvent = threading.Event()
        syslogPauseEvent = threading.Event()
        threadReadyEvent = threading.Event()

        # Open serial port and start syslog reader thread
        try:
            # One time syslog serial initialization
            syslog_serial_port = serial.Serial(SYSLOG_SERIAL_PORT_DEFINITION, 115200)
            if not syslog_serial_port.is_open:
                print("Serial Port failed to open")
                return

            syslog_reader_thread = threading.Thread(
                target=self.syslog_reader,
                args=(syslog_serial_port, dnldFinishedEvent, syslogPauseEvent))
            
            syslog_reader_thread.start()

        except (serial.SerialException, Exception) as e:
            print(f"Initialization exception: {e}")
            self.totDownloadTime = 0.0
            self.iteration_count = 0
            self.isAppRunning = False
            return

        # Coordinate downloads
        try:
            # Wait for syslog monitor thread to start
            while not self.isThreadRunning:
                time.sleep(0.1)

            # This begins the run loop
            while self.isAppRunning:
                # Check of last download completed testing
                self.iteration_count += 1
                if self.iteration_count > TARGET_ITERATION_COUNT:
                    return

                print("\n" + "="*20, end=" ")
                print(f"Iteration {self.iteration_count} ", end=" ")
                print("="*20)

                # Reset state for new iteration
                self.reached100Percent = False
                self.current_dnld_state = hcomDnldState.DnldWaitStart
                dnldFinishedEvent.clear()
                syslogPauseEvent.set()

                startDateTime = time.time()
                execCliReturn = self.run_cli_command()
                endDateTime = time.time()

                self.iterationExecTime = endDateTime - startDateTime
                self.totDownloadTime += self.iterationExecTime

                # If the syslog messages indicate that the download is
                # finished this wait() may block. But, if it does it will
                # block only for a short time because sometimes extra time
                # is needed for syslog to return the final syslog message.
                # Wait for syslog reader to finish
                dnldFinishedEvent.wait(POLL_SECONDS_BEFORE_FAILURE)

                # Determine result status
                reportState = self._determine_report_state(execCliReturn, dnldFinishedEvent)

                # Record result
                exeRetCodeValue[reportState.value] += 1

                if reportState == reportableCodes.Success:
                    self.success_count += 1
                    print(f"✓ Iteration {self.iteration_count} SUCCESS")
                else:
                    self.failure_count += 1
                    print(f"✗ Iteration {self.iteration_count} FAILURE")

                self.display_stats()

        except KeyboardInterrupt:
            print("\nStopping tool (Ctrl+C Detected)")

        except Exception as e:
            print(f"\nStopping unexpected exception: {e}")

        finally:
            self.iteration_count = max(0, self.iteration_count - 1)
            self.isAppRunning = False
            print("\nFinal Statistics:")
            self.display_final_stats()
            print(f"\nExecution has ended")
            self.totDownloadTime = 0.0

    def _determine_report_state(self, execCliReturn, dnldFinishedEvent):
        """Determine the reportable status from all available indicators"""
        
        # Check timeout
        if not dnldFinishedEvent.is_set():
            return reportableCodes.SyslogTimeout
        
        # Check execution time
        if self.iterationExecTime < MINIMUM_EXECUTION_TIME:
            return reportableCodes.ExTooFast
        
        # Check success condition
        if execCliReturn == exeCliRet.exeCliSuccess and self.current_dnld_state == hcomDnldState.DnldSuccess:
            return reportableCodes.Success
        
        # Check syslog failures
        if self.current_dnld_state == hcomDnldState.DnldFailure:
            return reportableCodes.HcomError
        if self.current_dnld_state == hcomDnldState.DnldActive:
            return reportableCodes.SyncError
        if not self.reached100Percent:
            return reportableCodes.NoSyslogEOF
        
        # Check CLI failures
        if execCliReturn == exeCliRet.exeCliNotFound:
            return reportableCodes.NoCLIFound
        if execCliReturn == exeCliRet.exeCliRunErr:
            return reportableCodes.CliRunError
        if execCliReturn == exeCliRet.exeCliRunExecp:
            return reportableCodes.CliRunExecp
        
        # Report unknown status
        print(f"Warning: Unknown CLI return value of {execCliReturn}")
        return reportableCodes.CliRunExecp


    def display_stats(self):
        """Display current statistics"""

        if self.iteration_count == 0:
            return          # Nothing to do

        total = self.success_count + self.failure_count
        success_rate = (self.success_count / total * 100) if total > 0 else 0
        downloadRate = (self.fileSize / self.iterationExecTime) / 1024
        dnldMin, dnldSec = divmod(self.iterationExecTime, 60)
        totDnldMin, totDnldSec = divmod(self.totDownloadTime, 60)

        print(f"DOWNLOAD STATISTICS - {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"File: {self.file_path}, size: {self.fileSize/1024:.1f} KB")
        print(f"Total Iteration: {self.iteration_count}")
        print(f"Total Successes: {self.success_count}")
        print(f"Total Failures : {self.failure_count}")
        print(f"Success Percent: {success_rate:.2f}%")
        print(f"Download time  : {dnldMin:02.0f}:{dnldSec:05.2f}")
        print(f"Aver KB/Second : {downloadRate:.1f} KB/Sec")
        print(f"Total Dnld Time: {totDnldMin:02.0f}:{totDnldSec:05.2f}")

        retCodeMaxOffset = len(exeRetCodeValue)
        for offset in range(0, retCodeMaxOffset):
            retVal = exeRetCodeValue[offset]
            if offset < retCodeMaxOffset - 1:
                print (f"{exeRetCodeString[offset]}:{retVal},", end=" ")
            else:
                print (f"{exeRetCodeString[offset]}:{retVal}")

    def display_final_stats(self):
        """Display summary statistics at the end"""

        if self.iteration_count == 0:
            return          # Nothing to do

        total = self.success_count + self.failure_count
        success_rate = (self.success_count / total * 100) if total > 0 else 0
        averageDownloadRate = ((self.fileSize * self.iteration_count) / self.totDownloadTime) / 1024
        exeTimeMin, exeTimeSec = divmod(self.totDownloadTime, 60)
        exeTimeHour, exeTimeMin = divmod(exeTimeMin, 60)

        print("="*60)
        print(f"FINAL STATISTICS - {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
        print(f"File: {self.file_path}, size: {self.fileSize/1024:.1f} KB")
        print(f"Iterations    : {self.iteration_count}")
        print(f"Successes     : {self.success_count}")
        print(f"Failures      : {self.failure_count}")
        print(f"Success Rate  : {success_rate:.2f}%")
        print(f"Aver KB/Second: {averageDownloadRate:.1f} KB/Sec")
        print(f"Execution Time: {exeTimeHour:1.0f}:{exeTimeMin:02.0f}:{exeTimeSec:02.2f}")

        retCodeMaxOffset = len(exeRetCodeValue)
        for offset in range(0, retCodeMaxOffset):
            retVal = exeRetCodeValue[offset]
            if offset < retCodeMaxOffset - 1:
                print (f"{exeRetCodeString[offset]}:{retVal},", end=" ")
            else:
                print (f"{exeRetCodeString[offset]}:{retVal}")

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
