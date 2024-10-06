# Last Changed 4Oct24
# This is a tool to use the 'git merge-file 'utility' to  do a 3-way merge
# from Legacy Meadow to the Meadow version that will use Nuttx V12.
# 
# See tje companion tool's source file for information on how to use
# these tools.

import subprocess
import csv

# This is the active Meadow.OS branch
rebaseRepoPath =   "/Users/lexas/upgrade-rebased-Meadow.OS/target/" # rebase while doing development work

# This is where each of the 3 repo can be found
# rebaseRepoPath =   "/home/peter/wl/RebaseRepos/RB.1-12Mar/"

previousRepoPath = "/Users/lexas/upgrade-rebased-Meadow.OS/v1.2/"
ongoingRepoPath =  "/Users/lexas/upgrade-rebased-Meadow.OS/v1.9/"

# Location of CSV file named 'MergeListData.csv'
csvListDataPath = "/Users/lexas/upgrade-rebased-Meadow.OS/dirTextFiles/"

# The condition field of the csv file will contain one of these values
COND_UNKNOWN_CONDITION    = 0    #This is the default 'condition' value
COND_ONLY_IN_ONGOING      = 1
COND_ONLY_IN_PREVIOUS     = 2
COND_NOT_IN_REBASE        = 3
COND_ONLY_IN_REBASE       = 4
COND_NOT_IN_PREVIOUS      = 5
COND_NOT_IN_ONGOING       = 6
COND_ALL_3_MERGE_OKAY     = 7
COND_PREVIOUS_REPEATED    = 8
COND_REBASE_REPEATED      = 9

condTypeNames = ["COND_UNKNOWN_CONDITION:",
                 "COND_ONLY_IN_ONGOING:  ",
                 "COND_ONLY_IN_PREVIOUS: ",
                 "COND_NOT_IN_REBASE:    ",
                 "COND_ONLY_IN_REBASE:   ",
                 "COND_NOT_IN_PREVIOUS:  ",
                 "COND_NOT_IN_ONGOING:   ",
                 "COND_ALL_3_MERGE_OKAY: ",
                 "COND_PREVIOUS_REPEATED:",
                 "COND_REBASE_REPEATED:  "]

# Add up the totals here
condTotals = [0,0,0,0,0,0,0,0,0,0]
numbMergeAttempt = 0

# Expect a file named "MergeListData.csv"
csvFilePathName = csvListDataPath + "MergeListData.csv"
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
        numbMergeAttempt += 1
        if(subProcResult.returncode == 0):
            continue      # 0 = merge with no conflicts

         # 1-127 = number of conflicts
        if(subProcResult.returncode > 0 and subProcResult.returncode < 128):
            print(f"{numbMergeAttempt} - {subProcResult.returncode} Merge conflict(s): `{rebasePN}`")
        else:     # Error
            print(f"{numbMergeAttempt} Error: {subProcResult}")

        # DEBUG VERSION
        # numbMergeAttempt += 1
        # if(subProcResult.returncode == 0):      # = 0 merge with no conflicts
        #     print(f"{numbMergeAttempt} Successfully merged:{rebasePN}")
        # elif(subProcResult.returncode > 0 and subProcResult.returncode < 128): # 1-127 = number of conflicts
        #     print(f"{numbMergeAttempt} - {subProcResult.returncode} Merge Conflicts in: {rebasePN}")
        # else:     # Error
        #     print(f"{numbMergeAttempt} Error: {subProcResult}")

    # This is the other automatic option. We copy the file from ongoing to
    # rebase, since we have all we need, and using git merge-file will
    # output an error.
    elif(conditionInt == COND_NOT_IN_PREVIOUS):
        # If no previous file then the file needs to be copied from
        # ongoing to rebase
        subProcResult = subprocess.run(["cp", "-r", ongoingPN, rebasePN])
        if(subProcResult.returncode == 0):
            continue      # 0 = copy successful

        print(f"File copy of `{ongoingPN}` to `{rebasePN}` returned:{subProcResult.returncode}")
    else:
        print(f"Ignoring `{rebasePN}`")

# Loop exit

# Show stats
for totalIndex, total in enumerate(condTotals):
    indexName = condTypeNames[totalIndex]
    print(f"{indexName}{total}")

f_mergeList.close()

