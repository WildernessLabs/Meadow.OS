# This is a simple tool to test using git merge-file to determine the best
# way it can be used in our Nuttx v12 rebase efforts.

import subprocess
import csv

# This is where each of the 3 repo can be found
# rebaseRepoPath =   "/home/peter/wl/" # rebase while doing development work
rebaseRepoPath =   "/home/peter/wl/RebaseTest1/RB.1-12Mar/"
previousRepoPath = "/home/peter/wl/RebaseTest1/M1.2-12Mar/"
ongoingRepoPath =  "/home/peter/wl/RebaseTest1/M1.9-12Mar/"

# Location of CSV file named 'MergeListData.csv'
csvListDataPath = "/home/peter/wl/RebaseRepos/dirTextFiles/"

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

condTypes = ["COND_UNKNOWN_CONDITION:",
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

    # if(conditionInt == COND_ONLY_IN_ONGOING):
    #     print("COND_ONLY_IN_ONGOING")
    # elif(conditionInt == COND_ONLY_IN_PREVIOUS):
    #     print("COND_ONLY_IN_PREVIOUS")
    # elif(conditionInt == COND_NOT_IN_REBASE):
    #     print("COND_NOT_IN_REBASE")
    # elif(conditionInt == COND_ONLY_IN_REBASE):
    #     print("COND_ONLY_IN_REBASE")
    # elif(conditionInt == COND_NOT_IN_PREVIOUS):
    #     print("COND_NOT_IN_PREVIOUS")
    # elif(conditionInt == COND_NOT_IN_ONGOING):
    #     print("COND_NOT_IN_ONGOING")
    # elif(conditionInt == COND_ALL_3_MERGE_OKAY):
    #     print("COND_ALL_3_MERGE_OKAY")
    # elif(conditionInt == COND_PREVIOUS_REPEATED):
    #     print("COND_PREVIOUS_REPEATED")
    # elif(conditionInt == COND_REBASE_REPEATED):
    #     print("COND_REBASE_REPEATED")
    # elif(conditionInt == COND_UNKNOWN_CONDITION):
    #     print("COND_UNKNOWN_CONDITION")
    # else:
    #     print(f"Unknown condition of:{condition}")

# It seems the 'git merge-file' command, without any command line options
# will modify the file 'current' with any merge conflicts. The -p option
# allows these merge conflicts to be sent to stdout instead
# 
# git merge-file [-L <current-name> [-L <base-name> [-L <other-name>]]]
# 	[--ours|--theirs|--union] [-p|--stdout] [-q|--quiet] [--marker-size=<n>]
# 	[--[no-]diff3] [--object-id] <current> <base> <other>
#
# subProcResult = subprocess.run(["git", "merge-file", "<current>", "<base>", "<other", "-p", "--diff-algorithm={histogram}"],
#                                capture_output=True, text=True)
    # Need full path name
    rebasePN = rebaseRepoPath + rebase
    ongoingPN = ongoingRepoPath + ongoing
    if(conditionInt == COND_NOT_IN_PREVIOUS):
        previousPN = ""
    else:
        previousPN = previousRepoPath + previous
        
    # if(conditionInt == COND_ALL_3_MERGE_OKAY or conditionInt == COND_NOT_IN_PREVIOUS):
    # subProcResult = subprocess.run(["git", "merge-file", rebase, previous, ongoing, "-p", "--diff-algorithm=histogram"],
    #                              capture_output=True, text=True)

    # Only process if all 3 files are available. Otherwise git merge-file reports an error
    if(conditionInt == COND_ALL_3_MERGE_OKAY):
        subProcResult = subprocess.run(["git", "merge-file", rebasePN, previousPN, ongoingPN],
                                     capture_output=True, text=True)
        if(subProcResult.returncode == 0):      # 0 = merge with no conflicts
            continue
        elif(subProcResult.returncode > 0 and subProcResult.returncode < 128):     # 1-127 = number of conflicts
            print(f"{numbMergeAttempt} - {subProcResult.returncode} Merge Conflicts in: {rebasePN}")
        else:     # Error
            print(f"{numbMergeAttempt} Error: {subProcResult}")

        # DEBUG VERSION
        # numbMergeAttempt += 1
        # if(subProcResult.returncode == 0):      # = 0 merge with no conflicts
        #     print(f"{numbMergeAttempt} Successfully merged:{rebasePN}")
        # elif(subProcResult.returncode > 0 and subProcResult.returncode < 128):     # 1-127 = number of conflicts
        #     print(f"{numbMergeAttempt} - {subProcResult.returncode} Merge Conflicts in: {rebasePN}")
        # else:     # Error
        #     print(f"{numbMergeAttempt} Error: {subProcResult}")

    else:
        print(f"Ignoring `{rebasePN}`")
# Loop exit

# Show stats
for totalIndex, total in enumerate(condTotals):
    indexName = condTypes[totalIndex]
    print(f"{indexName}{total}")

f_mergeList.close()

# --diff-algorithm={patience|minimal|histogram|myers}
# subProcResult = subprocess.run(["git", "merge-file", "<current>", "<base>", "<other", "-p", "--diff-algorithm={histogram}"]
#                                capture_output=True, text=True, cwd=absoluteFilePath)

# merge-file results
# CompletedProcess(args=['git', 'merge-file', 'Meadow.OS/apps/system/hcom/tests/hcom_dir_mgmt_tests.c', '', 'Meadow/apps/examples/hcom/tests/hcom_dir_mgmt_tests.c', '-p'], returncode=255, stdout='',
#                stderr='error: Could not stat Meadow.OS/apps/system/hcom/tests/hcom_dir_mgmt_tests.c: No such file or directory\n')