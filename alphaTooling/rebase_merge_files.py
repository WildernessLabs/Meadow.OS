# Last Changed 4Oct24
# Use this tool second, after using rebase_updater.py and after manually
# updating the resulting CSV file.
# This tool uses the 'git merge-file 'utility' to do a 3-way merge
# from Legacy Meadow to the Meadow version that will use Nuttx V12.
# 
# See the companion tool's source file for information on how to use
# these tools.
import subprocess
import csv

# Cloned for rebase merge
rebaseRepoPath =   "/home/peter/wl/Rebase-source-rebase-point-5/rebase/" 

# These 2 where cloned from a tag from the legacy Meadow repo
previousRepoPath = "/home/peter/wl/Rebase-source-rebase-point-5/previous/"
ongoingRepoPath =  "/home/peter/wl/Rebase-source-rebase-point-5/ongoing/"

# Location of Output and diagnostic files
csvListDataPath = "/home/peter/wl/Rebase-source-rebase-point-5/dirTextFiles/"

# The condition field of the csv file will contain one of these values
COND_UNKNOWN_CONDITION    = 0    #This is the default 'condition' value
COND_ONLY_IN_ONGOING      = 1
COND_ONLY_IN_PREVIOUS     = 2
COND_NOT_IN_REBASE        = 3
COND_ONLY_IN_REBASE       = 4
COND_NOT_IN_PREVIOUS      = 5
COND_NOT_IN_ONGOING       = 6
COND_ALL_3_MERGE_OKAY     = 7
COND_PREVIOUS_DUPLICATE   = 8
COND_REBASE_DUPLICATE     = 9

condTypeNames = ["COND_UNKNOWN_CONDITION: ",
                 "COND_ONLY_IN_ONGOING:   ",
                 "COND_ONLY_IN_PREVIOUS:  ",
                 "COND_NOT_IN_REBASE:     ",
                 "COND_ONLY_IN_REBASE:    ",
                 "COND_NOT_IN_PREVIOUS:   ",
                 "COND_NOT_IN_ONGOING:    ",
                 "COND_ALL_3_MERGE_OKAY:  ",
                 "COND_PREVIOUS_DUPLICATE:",
                 "COND_REBASE_DUPLICATE:  "]

# Add up the totals here
condTotals = [0,0,0,0,0,0,0,0,0,0]
linesProcessed = 0

# Expect a file named "MergeListDataCorrected.csv"
csvFilePathName = csvListDataPath + "MergeListDataCorrected.csv"
f_mergeList = open(csvFilePathName, "r")
csvFileReader = csv.reader(f_mergeList, delimiter=',')

for csvLineIndex, csvLine in enumerate(csvFileReader):
    condition  = csvLine[0]
    rebase     = csvLine[1]
    previous   = csvLine[2]
    ongoing    = csvLine[3]

    # Ignore CSV title line
    if(csvLineIndex == 0):
        continue

    # Tally the totals
    conditionInt = int(condition)
    condTotals[conditionInt] += 1

    # It seems the 'git merge-file' command, without any command line options
    # will modify the file 'current' with any merge conflicts. The -p option
    # allows these merge conflicts to be sent to stdout
    # 
    # git merge-file [-L <current-name> [-L <base-name> [-L <other-name>]]]
    # 	[--ours|--theirs|--union] [-p|--stdout] [-q|--quiet] [--marker-size=<n>]
    # 	[--[no-]diff3] [--object-id] <current> <base> <other>
    #
    # subProcResult = subprocess.run(["git", "merge-file", "<current>", "<base>", "<other", "-p", "--diff-algorithm={histogram}"]
    #                                capture_output=True, text=True, cwd=absoluteFilePath)
  
    # Need full path names, unless not in previous
    rebasePN = rebaseRepoPath + rebase
    ongoingPN = ongoingRepoPath + ongoing

    # Only use git merge-file if all 3 files are available.
    if(conditionInt == COND_ALL_3_MERGE_OKAY):
        previousPN = previousRepoPath + previous

        subProcResult = subprocess.run(["git", "merge-file", rebasePN, previousPN, ongoingPN],
                                     capture_output=True, text=True)
        # Show every line processed
        if(subProcResult.returncode == 0):      # = 0 merge with no conflicts
            print(f"{csvLineIndex} -  Successfully merged:{rebasePN}")
        elif(subProcResult.returncode > 0 and subProcResult.returncode < 128): # 1-127 = number of conflicts
            print(f"{csvLineIndex} - {subProcResult.returncode} Merge Conflicts:{rebasePN}")
        else:     # Error
            print(f"{csvLineIndex} - GIT Merge Error:{subProcResult}")

    # The other automatic option. We copy the file from ongoing to rebase,
    # since we have all we need. Using git merge-file will output an error.
    #
    # This copy will NOT create directories, if new directories are needed
    # they must be added  manually.
    elif(conditionInt == COND_NOT_IN_PREVIOUS):
        # If no previous file then the file needs to be copied from
        # ongoing to rebase
        subProcResult = subprocess.run(["cp", "-r", ongoingPN, rebasePN])
        if(subProcResult.returncode == 0):
            print(f"{csvLineIndex} - Successfully copied `{ongoingPN}` to `{rebasePN}`")
        else:
            print(f"{csvLineIndex} - File copied `{ongoingPN}` to `{rebasePN}` returned:{subProcResult.returncode}")
    else:
        print(f"{csvLineIndex} - Ignoring `{rebasePN}`")

# Loop exit

# Show stats
for totalIndex, total in enumerate(condTotals):
    indexName = condTypeNames[totalIndex]
    print(f"{indexName}{total}")

f_mergeList.close()

