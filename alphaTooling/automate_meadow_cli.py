#!/usr/bin/env python

# This will use CLIv2 to send a list of commands to HCOM. Command will be read from a file and the
# results will written to a file.
import subprocess
import sys
import time
import csv

# cmdFilePath = "C:/HcomFiles/CliHcomTest/CLI/Commands/TestFile.txt"
cmdFilePath = "C:/HcomFiles/CliHcomTest/CLI/Commands/AllCommands.txt"
# cmdFilePath = sys.argv[1]

# This is where the results are saved, one line per test
resultFilePath = "C:/HcomFiles/CliHcomTest/CLI/Results/"
# resultFile = sys.argv[2]

# Test file requirements. Each line starts with 'C' (command), 'P' (expect test to pass),
# 'F' (expect test to fail) or 'X' (Exit). Lines beginning with any other character are
# ignored. 'P' or 'F' entries are optional, but must follow it's related 'C' on the next
# line. The text of 'P' or 'F' tests are compared with the text returned from the 'C' command.
# If the 'P' or 'F' text is within the returned text, the test passes, if not test fails.

lineNumb = 0

# This is expected to be a complete path. The file name itself will be built using current time.
timestamp = time.strftime("%Y%m%d-%H%M%S-")
result_filename = resultFilePath + timestamp + "ResultFile.txt"
f_resultFile = open(result_filename, "w")

# Open the command file and read it at one time to solve the readline problem
# with detecting EOF
f_sourceFile = open(cmdFilePath, "r")
allTheLines = f_sourceFile.readlines()
linesRemaining =  len(allTheLines)
f_sourceFile.close

if linesRemaining == 0:
    print(f"Provided file {cmdFilePath} contains no information")

# Execute one line each loop
while linesRemaining > 0:
    linesRemaining = linesRemaining - 1

    sourceLine = allTheLines[lineNumb].strip()
    lineNumb = lineNumb + 1

    # Command
    if(sourceLine[0:1] == 'C'):
        cmdArray = ["meadow"]

        # Create an array of strings containing the command arguments
        commandText = sourceLine[1:].strip()  # Skip first and last
        cmdSplit = commandText.split()
        finalCmdArray = cmdArray + cmdSplit

        # Execute
        print(f"{lineNumb} - Executing {commandText} command")
        f_resultFile.write(f"{lineNumb} - Executing [{commandText}]\n")

        subProcResult = subprocess.run(finalCmdArray, capture_output=True, text=True)
        if(subProcResult.returncode != 0):
            # if "The operation has timed out" in subProcResult.stderr:
            #     print ("Timed out")
            print(f"{lineNumb} - Execution Error: returned{subProcResult.returncode}, Message:{subProcResult.stderr}")
            f_resultFile.write(f"{lineNumb} -   Command {commandText} Error with return:{subProcResult.returncode}, Message:{subProcResult.stderr}\n")
            break       # Exit
        
        recentCmd = True
        continue

    elif(sourceLine[0:1] == 'P' or sourceLine[0:1] == 'F'):
        print(f"{lineNumb} - Compare Test found [{sourceLine}]")
        if recentCmd:
            if(sourceLine[0:1] == 'P'):
                passOrFailTrue = True
            else:
                passOrFailTrue = False

            # Compare the results of the execution with the provided text and determine
            # if this represents test passed or failed
            resultCheckStr = sourceLine[1:].strip()
            if resultCheckStr in subProcResult.stdout:
                if(passOrFailTrue):
                    testPassed = True
                else:
                    testPassed = False
            else:
                if(passOrFailTrue):
                    testPassed = False
                else:
                    testPassed = True
            
            if(testPassed):
                print(f"{lineNumb} - Test Passed! For command [{commandText}]")
                f_resultFile.write(f"{lineNumb} -   Command [{commandText}] Passed\n")
            else:
                if(passOrFailTrue):
                    print(f"{lineNumb} - Test FAILED {resultCheckStr} not in {subProcResult.stdout}")
                    f_resultFile.write(f"{lineNumb} -   Command [{commandText}] Failed! {resultCheckStr} not in {subProcResult.stdout}\n")
                else:
                    print(f"{lineNumb} - Test Chk FAILED {resultCheckStr} found in {subProcResult.stdout}")
                    f_resultFile.write(f"{lineNumb} -   Command [{commandText}] Failed! {resultCheckStr} found in {subProcResult.stdout}\n")

            # Used the test information line, need next line
            recentCmd = False
            continue

        else:
            print(f"{lineNumb} - No recent command, ignoring out of sequence P or T ({sourceLine})\n")
            continue

    elif(sourceLine[0:1] == 'X'):
        print(f"{lineNumb} - Exit\n")
        f_resultFile.write(f"{lineNumb} - Exit [{sourceLine}]\n")
        break

    else:
        # All other lines are ignored
        recentCmd = False
        print(f"{lineNumb} - Ignoring this line [{sourceLine}]")
        f_resultFile.write(f"{lineNumb} - Ignoring [{sourceLine}]\n")
        continue
    
f_resultFile.close

