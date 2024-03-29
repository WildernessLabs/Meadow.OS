# This is a simple tool to test using git merge-file to determine the best
# way it can be used in our Nuttx v12 rebase efforts.

import subprocess
import csv

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

# This is where each of the 3 repo can be found
rebaseRepoPath =   "/home/peter/wl/RebaseRepos/RB.1-12Mar/"
previousRepoPath = "/home/peter/wl/RebaseRepos/M1.2-12Mar/"
ongoingRepoPath =  "/home/peter/wl/RebaseRepos/M1.9-12Mar/"

# Output files and diagnostic files
finalCsvListPath = "/home/peter/wl/RebaseRepos/dirTextFiles/"

# Expect a file named "MergeListData.csv"
f_mergeList = open(finalCsvListPath + "MergeListData.csv", "r")
csvFileReader = csv.reader(f_mergeList, delimiter=',')
for csvLine in csvFileReader:
    condition  = csvLine[0]
    rebase     = csvLine[1]
    previous   = csvLine[2]
    ongoing    = csvLine[3]

    # Tally the totals
    conditionInt = int(condition)
    condTotals[conditionInt] += 1

    if(conditionInt == COND_ONLY_IN_ONGOING):
        print("COND_ONLY_IN_ONGOING")
    elif(conditionInt == COND_ONLY_IN_PREVIOUS):
        print("COND_ONLY_IN_PREVIOUS")
    elif(conditionInt == COND_NOT_IN_REBASE):
        print("COND_NOT_IN_REBASE")
    elif(conditionInt == COND_ONLY_IN_REBASE):
        print("COND_ONLY_IN_REBASE")
    elif(conditionInt == COND_NOT_IN_PREVIOUS):
        print("COND_NOT_IN_PREVIOUS")
    elif(conditionInt == COND_NOT_IN_ONGOING):
        print("COND_NOT_IN_ONGOING")
    elif(conditionInt == COND_ALL_3_MERGE_OKAY):
        print("COND_ALL_3_MERGE_OKAY")
    elif(conditionInt == COND_PREVIOUS_REPEATED):
        print("COND_PREVIOUS_REPEATED")
    elif(conditionInt == COND_REBASE_REPEATED):
        print("COND_REBASE_REPEATED")
    elif(conditionInt == COND_UNKNOWN_CONDITION):
        print("COND_UNKNOWN_CONDITION")
    else:
        print(f"Unknown condition of:{condition}")

f_mergeList.close()

for totalIndex, total in enumerate(condTotals):
    indexName = condTypes[totalIndex]
    print(f"{indexName}{total}")

# readResult = f_mergeList.readline()

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

# f_mergeList.close()


# --diff-algorithm={patience|minimal|histogram|myers}
# subProcResult = subprocess.run(["git", "merge-file", "<current>", "<base>", "<other", "-p", "--diff-algorithm={histogram}"]
#                                capture_output=True, text=True, cwd=absoluteFilePath)

# find "<directory> -type f >> M1.2Files.txt
# subProcResult = subprocess.run(["find", ".", "-name", "*.c", "-or", "-name", "*.h", "-type", "f"],
#                         capture_output=True, text=True, cwd=absoluteFilePath)
