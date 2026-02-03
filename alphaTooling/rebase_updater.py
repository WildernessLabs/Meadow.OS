# Last Changed 30Jan2026
# Use this tool after cloning the 3 version of meadow. See below
# for additional instructions. 

import subprocess
# import pathlib
from dataclasses import dataclass
import time
import csv

# Specific paths depend on the way the 3 repos are organized
# Note: the 'current', 'base', and 'other' terms defined by the
# 'git merge-file' documentation are not used. Instead the terms
# used are 'rebase', 'previous' and 'ongoing' as these better
# describe the usage in this application.

# The 3 repos are:
#  rebaseRepoPath - This is the current Rebase to update (i.e. a recently
#    cloned Meadow.OS rebase).
#  previousRepoPath - This code was cloned from the Legacy Meadow at the time
#    Meadow.OS rebase was last updated (previous update).
#  ongoingRepoPath - This is a snapshot of the current Legacy Meadow repo.
#
# Peter 03Feb2026
# rebaseRepoPath - is a clone if Meadow Rebase at the time of the Merge
rebaseRepoPath =   "/home/peter/wl/Meadow.OS"

# These 2 are cloned from tags in the legacy Meadow repo
# previousRepoPath is from source-rebase-point-4
previousRepoPath = "/home/peter/wl/Rebase-source-rebase-point-5/previous/"

# previousRepoPath is from source-rebase-point-5
ongoingRepoPath =  "/home/peter/wl/Rebase-source-rebase-point-5/ongoing/"
# Output and diagnostic files
finalCsvListPath = "/home/peter/wl/Rebase-source-rebase-point-5/tempProcFiles/"

# Used by Alexis on his machine in early 2024
# rebaseRepoPath =   "/Users/lexas/upgrade-rebased-Meadow.OS/target/" # rebase while doing development work
# These point to a set of repos for doing dev work
# previousRepoPath = "/Users/lexas/upgrade-rebased-Meadow.OS/v1.2/" # previous
# ongoingRepoPath =  "/Users/lexas/upgrade-rebased-Meadow.OS/v1.9/" # ongoing
# Output files and diagnostic files
# finalCsvListPath = "/Users/lexas/upgrade-rebased-Meadow.OS/tempProcFiles/"

# ------------------------------------------------------------
absRepoPaths = [rebaseRepoPath, previousRepoPath, ongoingRepoPath]

# There are some exclusions (such as sqlite and mono) that can be added as
# Meadow.OS matures.
# The following are all the places Meadow rebase files are found
rebaseRepoPaths = ["Meadow.OS/apps/system/hcom",
                   "Meadow.OS/nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/include",
                   "Meadow.OS/nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/kernel",
                   "Meadow.OS/nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/scripts",
                   "Meadow.OS/nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/src",
                   "Meadow.OS/Meadow.OS",  # Meadow/nuttx/configs/stm32f777zit6-meadow/src
                   # Not sure about this directory, Legacy has 1 file here there are 4?
                   "Meadow.OS/nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/configs/meadow_os"]
#                  "Meadow.OS/nuttx/boards/arm/stm32f7/stm32f777zit6-meadow/mono",

# Legacy Meadow last used for Nuttx v12 previous (current)
previousRepoPath = ["Meadow/apps/examples/hcom",
                    "Meadow/nuttx/configs/stm32f777zit6-meadow/include",
                    "Meadow/nuttx/configs/stm32f777zit6-meadow/kernel",
                    "Meadow/nuttx/configs/stm32f777zit6-meadow/scripts",
                    "Meadow/nuttx/configs/stm32f777zit6-meadow/src",
                    "Meadow/nuttx/include/meadow"]
                    # "Meadow/nuttx/configs/stm32f777zit6-meadow/mono"]

# Legacy Meadow ongoing (base) development
ongoingRepoPaths = ["Meadow/apps/examples/hcom",
                    "Meadow/nuttx/configs/stm32f777zit6-meadow/include",
                    "Meadow/nuttx/configs/stm32f777zit6-meadow/kernel",
                    "Meadow/nuttx/configs/stm32f777zit6-meadow/scripts",
                    "Meadow/nuttx/configs/stm32f777zit6-meadow/src",
                    "Meadow/nuttx/include/meadow"]
                    # "Meadow/nuttx/configs/stm32f777zit6-meadow/mono"]

# Put all the repo path lists into a single list
repoPathLists = [rebaseRepoPaths, previousRepoPath, ongoingRepoPaths]

# Create lists to accept the file names from each path
rebaseFileNamesList   = []
previousFileNamesList = []
ongoingFileNamesList  = []
repoFileNameLists = [rebaseFileNamesList, previousFileNamesList,
                     ongoingFileNamesList]

# The following has been copied from work done by Alexis and explains
# a method for keeping the legacy files organized.
# -------------------------------------------------------------------
# Using Python tools to bring changes from Legacy Meadow to Rebase

#  a. N = Our current “source rebase point”. An incrementing number indicating
#     the latest point where our code was upgraded. Right now for example, we
#     are at 'source-rebase-point-4' from Feb 21st, so N=4

#  b. In your legacy Meadow.OS workspace dir, go to the latest commit on main
#     and git tag it as 'source-rebase-point-[N+1]' . git push the tag.

#    Create a tag: git tag branch source-rebase-point-N
#    Push the tag: git push origin --tags
#    Note below using '--single-branch' is faster but leaves in a detached head state
#    - navigate to: '~/wl/RebaseRepos<date>-tag-[N]/previous'
#    - git clone --branch source-rebase-point-[N] https://github.com/WildernessLabs/Meadow.git
#    - navigate to: '~/wl/RebaseRepos<date>-tag-[N+1]/ongoing'
#    - git clone --branch source-rebase-point-[N+1] https://github.com/WildernessLabs/Meadow.git

#  c. Create a root directory for this process, something like rebase-upgrade.
 
#  d. In your root, create directories 'previous' 'ongoing' and 'rebase':
#      - previous - git clone Legacy Meadow.OS here so that ends up in
#        previous/Meadow - then git checkout source-rebase-point-N.

#      - ongoing - git clone Legacy Meadow.OS here so that it is in
#        ongoing/Meadow - then git checkout source-rebase-point-[N+1].

#      - rebase - git clone Rebase Meadow.OS here so that it is in
#        rebase/Meadow.OS - you should already be placed on latest main commit,
#        which is correct

#      - tempProcFiles - this directory if used to store files needed during
#        processing.l
    
# You should be ready to run the Python scripts, which must be modified to
#  correctly point to the paths you created above. 

#  After the above steps are followed to create 4 sub-directories
# 
#  this tool creates a list of files which must be manually edited to produce
# a list useable for doing the merge.

# ----------------------------------------------------------------------------
# In the following table describes possible condition values and action needed
# based on the condition code generated by this tool. 
# 
#     V12   ---Legacy---
#  	Rebase  Prev Ongoing Action     Comment
# 0.   0     0      0    Ignore     Not possible, unused pattern
# 1.   0     0      1    Inspect    New file recently added to Legacy  (May need to manually add, no destination)
# 2.   0     1      0    Ignore     Prev file deleted since last merge (If only Prev, it's not needed in Rebase)
# 3.   0     1      1    Inspect    File not in Rebase yet             (Why not in rebase, renamed?)
# 4.   1     0      0    Ignore     Only Rebase, added to Rebase       (Name changed or added directly to Rebase)
# 5.   1     0      1    Tool       New in Legacy, will copy to rebase (Tool will copy file to rebase)
# 6.   1     1      0    Inspect    Deleted, moved, renamed in ongoing (Very likely should be deleted)
# 7.   1     1      1    Tool       Desired result, just merge         (3-way merge, since all 3 files known)
#
# Note: If the above 'Action' column contains 'Inspect' these rows in the
#   'MergeListData.csv' will need to be individually inspected and the
#   appropriate.
#
#  1. Recommended, make a copy of the MergeListData.csv. It will be modified.
#  2. Check the 'DuplicateEntries.txt' file duplicate files in Meadow Rebase.
#   Take appropriate action to rectify the duplicate issue.
#  3. Using a spreadsheet program import the MergeListData.csv.
#  4. If a row in the 'MergeListData.csv' file contains a 'Condition' value
#   of 1, 3 or 6, (in rare cases 8 and 9) some change(s) is/are needed. Both
#   to the files and to the 'MergeListData.csv' file data. The ultimate goal
#   is to end up with only 4, 5 or 7 in the 'MergeListData.csv' file's
#   'Conditions' column.
#   There are instructions following to give help in explaining the problem
#   and suggestions on how to correct it.
#  5. Once all the rows in the 'MergeListData.csv' file have been corrected
#   launch the 'rebase_merge_files.py' tool to do the actual merging of the
#   Rebase source code.
#  6. After the 'rebase_merge_files.py' tool has finished the tool's output
#   will indicate the action needed. If all went well the only remaining work
#   will be merge conflicts. The tool has, at each merge conflict, modified 
#   the source file's code, as would be done for any merge conflict.
#   Other errors are possible such as file copy problems or the git merge-tool
#   experiencing other problems with a file.
#
# Suggestions fixing conditions:
#  It's okay to have a different file name in the rebase column than in the
#  legacy files.
#
# Fix Condition 1 (New File) - this Condition is common and is corrected by
#  deciding were the file should go in Rebase and adding the path to the
#  Rebase column. Then in the Condition column change the '1' to a '5' and
#  let the tool copy the file.
#  One way to do this is to find the file in Legacy Meadow, then find a file
#  in the same directory and find this other file in Rebase Meadow. Then put
#  the new file in the same directory as the one just found.
# 
# Fix Condition 3 (Not in Rebase) - this condition is usually caused when a
#  file is delete or renamed in the Rebase. This will require investigation
#  and likely possibly manual changes, both to Rebase file system and to
#  the 'MergeListData.csv' file.
#
# Fix Condition 6 (not in ongoing) - this conditions exists when a file is
#  deleted, moved or renamed in ongoing. Different actions may be required
#  in each case.
#
# Fix Condition 9 (likely duplicate) - Delete the bogus file or rename one of
#  the files to there are not longer duplicates.
#
# Specific situation example:
# 1. ntpclient - There were 2 version in legacy Meadow of 'ntp_client.c' and
#  'ntp_client.h'. One used by Nuttx and one used by Meadow. One of these has
#  been renamed in Rebase Meadow to 'meadow_ntpclient.c & h'. This was to 
#  prevent duplicates in Rebase. However, this causes 'rebase_updater.py' tool
#  to place Rebase's meadow_ntpclient.c & h in a separate CSV row (condition 3)
#  from Legacy ntp_client.c & h (condition 4). Manually, modified CSV file so
#  all are in the same row with condition 7, ready to merge. Rebase column
#  file names 'meadow_ntpclient.c' and 'meadow_ntpclient.h'
#  Ongoing and previous file names 'ntp_client.c' and 'ntp_client.h'.

# Conditions
# The following are used to define the rebase constants
COND_UNKNOWN_CONDITION    = 0   # default 'condition' value
COND_ONLY_IN_ONGOING      = 1
COND_ONLY_IN_PREVIOUS     = 2
COND_NOT_IN_REBASE        = 3
COND_ONLY_IN_REBASE       = 4
COND_NOT_IN_PREVIOUS      = 5
COND_NOT_IN_ONGOING       = 6
COND_ALL_3_MERGE_OKAY     = 7
COND_PREVIOUS_REPEATED    = 8   # Unexpected
COND_REBASE_REPEATED      = 9   # Unexpected

# ----------------------------------------------------------------------------
# This defines the data collected and analyzed for the 3 files
@dataclass
class csvEntry():
    # indicates one entries condition
    condition: int          = COND_UNKNOWN_CONDITION

    # Full path and file name of the file
    rebasePathName:   str   = ""
    previousPathName: str   = ""
    ongoingPathName:  str   = ""

#---------------------------------------------------------------
# Save all the *.c and *.h files in the 3 repos into 3 lists for processing.
# To help isolate files in /nuttx/ from those in /apps/, as the lists
# are built the letter a will be post-pended to those in /apps/
repoIndex = 0

# Go through the 3 repos and read all the files found in the specified locations
# (see above) There are 3 locations which will result in 3 lists for processing.
# The only additional processing done here is to add an '*' at the end of all
# files found in a '/apps/' directory. This will prevent files with the same
# name, but different repos from being matched.
for singleRepoPathList in repoPathLists:
    # Isolate a single repo file name list
    currentRepoFileNames = repoFileNameLists[repoIndex]

    # Find a single file location and read the files in its subdirectories
    for singlePath in singleRepoPathList:
        isAppsDir = False
            
        # if this is from an 'apps' directory we want to know remember this
        if(singlePath.find("/apps/") > 0):
            isAppsDir = True

        absoluteFilePath = absRepoPaths[repoIndex] + singlePath
        result = subprocess.run(["find", ".", "-name", "*.c", "-or", "-name", "*.h", "-type", "f"],
                                capture_output=True, text=True, cwd=absoluteFilePath)

        if result.returncode < 0:
            print(f"Error: {result.returncode} at line {repoIndex - 1}")
            continue

        # At this point most of the returned data consists of strings
        # containing several file names separated by new lines (\n).
        # We'll split the file names out and put them in the 3 lists

        # Separate the individual file names and process 1-by-1
        tempFileNames = []
        tempFileNames += result.stdout.split('\n')
        for singleFileName in tempFileNames:
            # Don't add lines without an actual file name data
            if(len(singleFileName) > 2):
                # Need to remove the first character or we'll have '.' in the
                # final string thanks to the 'subprocess.run(["find",...'
                singleFileNameLen = len(singleFileName) - 1
                # We want the original repo path added
                finalFileName = singlePath + singleFileName[-singleFileNameLen:]

                if(isAppsDir):
                    finalFileName = finalFileName + '*'
                currentRepoFileNames.append(finalFileName)

    # case insensitive sort, otherwise upper case is before all lower case
    currentRepoFileNames = sorted(currentRepoFileNames, key=lambda s: s.casefold())
    repoIndex += 1

# #---------------------------------------------------------------
# # diagnostic - write the results into 3 files for inspection
# diagFileNames = ["rebaseDiag", "previousDiag", "ongoingDiag"]
# diagTimestamp = time.strftime("%Y%m%d-%H%M%S-")
# repoIndex = 0

# # For each of the 3 repos lists get each list of files
# for singleRepoList in repoFileNameLists:
#     total_files = 0
#     entries_count = 0

#     # Get the list of files for a single repo
#     activeRepoFileNames = repoFileNameLists[repoIndex]

#     # Create a unique name for the diagnostic file
#     diagFileName = diagFileNames[repoIndex]
#     diag_file_name = finalCsvListPath + diagTimestamp + diagFileName + ".txt"

#     diag_file = open(diag_file_name, "w")

#     for singleFileName in activeRepoFileNames:
#         if(len(singleFileName) > 2):
#             diag_file.write(singleFileName + '\n')
#             entries_count += 1
#     diag_file.close()

#     print(f"FileName:{diag_file_name} Total files:{entries_count}")
#     repoIndex += 1
# # diagnostic - end

#---------------------------------------------------------------
# Find within each repo what filenames occur in more than one
# place. While this is not an illegal conditions it is one that
# may need to be invested and could cause tool confusion.
timestamp = time.strftime("%Y%m%d-%H%M%S-")
duplicateFileName = finalCsvListPath + timestamp + "DuplicateEntries.txt"
duplicate_file = open(duplicateFileName, "w")
duplicateCount = 0

# Check each repo to find duplicate files
for singleRepoList in repoFileNameLists:
    # Now working on a single repo
    for matchIndex, matchName in enumerate(singleRepoList):
        # isolate just the file name
        matchIndex = matchName.rfind("/")
        matchFileNameNoPath = matchName[matchIndex + 1:]

        for searchIndex, searchName in enumerate(singleRepoList):
            # Isolate the file name
            searchIndex = searchName.rfind("/")
            searchFileNameNoPath = searchName[searchIndex + 1:]

            # Compare just the file names without the path
            if(matchFileNameNoPath == searchFileNameNoPath and
               matchIndex != searchIndex):              # only show once
                    duplicateCount += 1
                    duplicate_file.write(matchName + '\n')

duplicate_file.close()

#---------------------------------------------------------------
# The last step is to combine these 3 lists in a single file. The result will
# be csv and contain 5 columns: column #, status, rebase, previous and ongoing
# We use 'ongoing' as our baseline for building the information needed by
# 'git merge-file'. Why? it has the most recent legacy changes.
consolidatedFileList = []
duplicateFileList = []

# Look at all the files in the ongoing list
for ongoingIndex, ongoingFileName in enumerate(ongoingFileNamesList):
    
    # Begin populating the internal list of consolidated that is a
    # consolidated list of all 3 repos
    tempEntry = csvEntry()
    tempEntry.ongoingPathName = ongoingFileName
    # tempEntry.ongoingIndex = ongoingIndex

    # isolate the file name and remove '/' so we can match it
    fileNameOnlyPos = ongoingFileName.rfind("/")
    origFileNameNoPath = ongoingFileName[fileNameOnlyPos + 1:]

    # Look for a matching file in the previous list
    prevMatchFound = 0
    for prevIndex, prevFileName in enumerate(previousFileNamesList):
        lastSlashPos = prevFileName.rfind("/")
        tempFileNameNoPath = prevFileName[lastSlashPos + 1:]

        if(origFileNameNoPath == tempFileNameNoPath):
            # found matching name
            tempEntry.previousPathName = prevFileName
            prevMatchFound += 1

    # Look for a matching file in the rebase list
    rebaseMatchFound = 0
    for rebaseIndex, rebaseFileNm in enumerate(rebaseFileNamesList):
        lastSlashPos = rebaseFileNm.rfind("/")
        tempFileNameNoPath = rebaseFileNm[lastSlashPos + 1:]

        if(origFileNameNoPath == tempFileNameNoPath):
            # found matching name
            tempEntry.rebasePathName = rebaseFileNm
            rebaseMatchFound += 1

    # Need to capture the current condition, if known
    if(prevMatchFound == 1):
        if(rebaseMatchFound == 1):
            tempEntry.condition = COND_ALL_3_MERGE_OKAY
        elif (rebaseMatchFound == 0):
            tempEntry.condition = COND_NOT_IN_REBASE
        else:
            tempEntry.condition = COND_REBASE_REPEATED
    elif(prevMatchFound == 0): 
        if(rebaseMatchFound == 1):
            tempEntry.condition = COND_NOT_IN_PREVIOUS
        elif(rebaseMatchFound == 0):
            tempEntry.condition = COND_ONLY_IN_ONGOING
        else:
            tempEntry.condition = COND_PREVIOUS_REPEATED

    consolidatedFileList.append(tempEntry)

# ---------------------------------------------------------------
# The 'ongoing' list has been exhausted. Therefore, most conditions have
# been found. But, there are still 'previous' and 'rebase' file lists and
# find which files are in the consolidated list because they were not in
# the on ongoing list.
tempPrevFileDict = {}

for prevIndex, prevFileName in enumerate(previousFileNamesList):
    isMatch = False

    # Only use the file name for compare
    lastSlashPos = prevFileName.rfind("/")
    prevFilNameNoPath = prevFileName[lastSlashPos + 1:]

    for ongoingFileName in ongoingFileNamesList:
        lastSlashPos = ongoingFileName.rfind("/")
        ongoingFileNameNoPath = ongoingFileName[lastSlashPos + 1:]
        if (prevFilNameNoPath == ongoingFileNameNoPath):
            isMatch = True  # Found match in ongoingFileNamesList
            break

    # We reach this point either when we've found a match or have looked at
    # all the entries in ongoing and not found a match. This is indicated by
    # isMatch being False. So, we'll add it to a list until rebase is tested
    if (not isMatch):
        # Looks like we found a missing entry
        # Not in ongoing file list, need to add it here
        tempPrevEntry = csvEntry()
        tempPrevEntry.condition = COND_ONLY_IN_PREVIOUS
        tempPrevEntry.previousPathName = prevFileName
        tempPrevFileDict[prevFilNameNoPath] = prevFileName

# ---------------------------------------------------------------
# Repeat for rebase file list
tempRebaseFileDict = {}

for rebaseIndex, rebaseFileName in enumerate(rebaseFileNamesList):
    isMatch = False

    # Only use the file name for compare
    lastSlashPos = rebaseFileName.rfind("/")
    rebaseFileNameNoPath = rebaseFileName[lastSlashPos + 1:]

    for ongoingFileName in ongoingFileNamesList:
        lastSlashPos = ongoingFileName.rfind("/")
        origFileNameNoPath = ongoingFileName[lastSlashPos + 1:]
    
        if rebaseFileNameNoPath == origFileNameNoPath:
            isMatch = True  # Found in ongoingFileNamesList
            break           # Keep checking

    # Finished checking if isMatch false we failed to find it
    if (not isMatch):
        # Not in ongoing file list but need to add it
        tempRebaseEntry = csvEntry()
        tempRebaseEntry.condition = COND_ONLY_IN_REBASE
        tempRebaseEntry.rebasePathName = rebaseFileName
        tempRebaseFileDict[rebaseFileNameNoPath] = rebaseFileName

#-----------------------------------------------------------------
# We have 2 dictionaries containing previous and rebase entires which have
# no counter part in ongoing.
# For those cases where previous and rebase contain the same file name,
# we combine them into a single entry. Otherwise, each gets it's own entry.
# Get the 'intersection' between the 2 (only in both)
inBothPrevRebase = tempPrevFileDict.keys() & tempRebaseFileDict.keys()
for singleKey in inBothPrevRebase:
    tempEntry = csvEntry()
    tempEntry.condition = COND_NOT_IN_ONGOING
    tempEntry.previousPathName  = tempPrevFileDict[singleKey]
    tempEntry.rebasePathName    = tempRebaseFileDict[singleKey]
    consolidatedFileList.append(tempEntry)

# Get the 'symmetric difference' between the 2 (only in one or the other)
inPrevOrRebase = (tempPrevFileDict.keys()) ^ (tempRebaseFileDict.keys())
for singleKey in inPrevOrRebase:
    tempEntry = csvEntry()
    if(tempRebaseFileDict.get(singleKey)):
        tempEntry.condition = COND_ONLY_IN_REBASE
        tempEntry.rebasePathName    = tempRebaseFileDict[singleKey]

    if(tempPrevFileDict.get(singleKey)):
        tempEntry.condition = COND_ONLY_IN_PREVIOUS
        tempEntry.previousPathName  = tempPrevFileDict[singleKey]
 
    consolidatedFileList.append(tempEntry)

#------------------------------------------------------------------------
# Need to remove all the '*' characters added to identify /apps/ files
for consolidatedIndex, consolidateEntry in enumerate(consolidatedFileList):
    isLineChangeNeeded = False

    # Check each entry for a string ending with an asterisk
    rebaseFileName   = consolidateEntry.__dict__["rebasePathName"]
    previousFileName = consolidateEntry.__dict__["previousPathName"]
    ongoingFileName  = consolidateEntry.__dict__["ongoingPathName"]

    if(len(rebaseFileName) > 2):
        matchIndex = rebaseFileName.rfind("*")
        if(matchIndex != -1):
            rebaseFileName = rebaseFileName[:matchIndex]
            isLineChangeNeeded = True

    if(len(previousFileName) > 2):
        matchIndex = previousFileName.rfind("*")
        if(matchIndex != -1):
            previousFileName = previousFileName[:matchIndex]
            isLineChangeNeeded = True

    if(len(ongoingFileName) > 2):
        matchIndex = ongoingFileName.rfind("*")
        if(matchIndex != -1):
            ongoingFileName = ongoingFileName[:matchIndex]
            isLineChangeNeeded = True

    # Do we need to replace the existing entry?
    if(isLineChangeNeeded):
        # Create a new entry to replace the one needing change
        tempEntry = csvEntry()
        tempEntry.condition = consolidateEntry.__dict__["condition"]
        tempEntry.rebasePathName   = rebaseFileName
        tempEntry.previousPathName = previousFileName
        tempEntry.ongoingPathName  = ongoingFileName
        
        # Replace the entry with the new one which is '*' free
        consolidatedFileList[consolidatedIndex] = tempEntry

# ---------------------------------------------------------------
# Output the final consolidated list to a csv file.
# Create a unique name for diagnostic file
timestamp = time.strftime("%Y%m%d-%H%M%S-")

# csvPathNameDbg = finalCsvListPath + timestamp + "MergeListDbg.csv"
# with open(csvPathNameDbg, 'w', newline="") as csvfile:
#     csvWriter = csv.writer(csvfile, delimiter='\n', quoting=csv.QUOTE_MINIMAL)
#     csvWriter.writerow(consolidatedFileList)

# Output just the data without the human readable information
csvPathNameData = finalCsvListPath + timestamp + "MergeListData.csv"
csvDataFile = open(csvPathNameData, "w")

# Header
csvDataLine = "Condition,Rebase,Previous,Ongoing"
csvDataFile.write(csvDataLine + '\n')
# Data
for consoLine in consolidatedFileList:
    condition  = consoLine.__dict__["condition"]
    rebase     = consoLine.__dict__["rebasePathName"]
    previous   = consoLine.__dict__["previousPathName"]
    ongoing    = consoLine.__dict__["ongoingPathName"]
    csvDataLine = f"{condition},{rebase},{previous},{ongoing}"
    csvDataFile.write(csvDataLine + '\n')

csvDataFile.close()
