# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

# Scripted utilities for Maya XMesh Saver Deadline Submission

###############################################################################
# Deadline Utility Functions
###############################################################################

from __future__ import print_function

import errno
import maya.cmds
import maya.mel
import os
import sys
import subprocess
import traceback

from XMeshUtils import *

##########
#Strings
##########

_s = {
    "noDeadlineMsg"     : "DEADLINE WAS NOT DETECTED ON YOUR SYSTEM!\nNOTE: Deadline provides a FREE MODE supporting up to 2 NODES without limitations.\nYou could install Deadline in FREE MODE to take advantage of network saving.\nYou can download Deadline from the link below:\n",
    "downloadAddress"   : "http://www.thinkboxsoftware.com/deadline-downloads/",
    "detectionErrorMsg"  : "\nAn error occurred while attempting to detect Deadline. Please see the Script Editor for more information.\nPlease contact us at support@thinkboxsoftware.com, and send us a copy of the error message that\nappears in the Script Editor\n",
    }

class DeadlineError(Exception):
    pass

class DeadlineNotFoundError(Exception):
    pass

class DeadlineInvokeError(Exception):
    pass

class deadlineSettings():
    def __init__(self):
        self.d = {
            'Plugin'                :   'MayaBatch',
            'Name'                  :   'XMeshMayaScriptJob',
            'Comment'               :   '',
            'MachineLimit'          :   0,
            'Priority'              :   50,
            'Frames'                :   '1',
            'OutputDirectory0'      :   ''
        }
        self.p = {
            'ScriptJob'             :   True,
            'ScriptFilename'        :   'XMeshSaverParams.py',
            'ScriptData'            :   'XMeshSaverParams.py',
            'ProjectPath'           :   getProjectPath(),
            'IncludeSceneFile'      :   False,
            'StrictErrorChecking'   :   True,
            'Version'               :   getMayaVersion(),
            'Build'                 :   "none",

        }

    def getDeadlineSettings(self):
        return os.linesep.join('%s=%s' % (k, v) for (k, v) in self.d.items())
    def getMayaPluginSettings(self):
        return os.linesep.join('%s=%s' % (k, v) for (k, v) in self.p.items())


def isInstalled():
    try:
        safeDeadlineCommand("About")
        return True
    except DeadlineError as e:
        print("// XMesh: Deadline error:", str(e))
        return False
    except DeadlineNotFoundError:
        return False
    except DeadlineInvokeError as e:
        raise
        

# Verifies the slashes are consistent in a given filename. A useful utility function.
def checkSlashes(fileName):
    newResult = fileName.replace('\\', os.sep)
    newResult = newResult.replace('/', os.sep)
    return newResult

# Returns True if the path is on C:, D:, or E:
def isLocalDrive(path):
    if len(path) > 0:
        if path.startswith(('C', 'c', 'D', 'd', 'E', 'e')):
            return True
    return False

def safeDeadlineCommand(command):
    result = ''

    creationflags = 0

    #windows
    if os.name is 'nt':
        # still show top-level windows, but don't show a console window
        CREATE_NO_WINDOW = 0x08000000   #MSDN process creation flag
        creationflags = CREATE_NO_WINDOW

        dlc = 'deadlinecommand.exe'

        deadlineCommandPath = os.getenv('DEADLINE_PATH', os.getenv('programfiles', '') + '\\Thinkbox\\Deadline10\\bin')
        if not os.path.isdir(deadlineCommandPath):
            #deadline 5
            deadlineCommandPath = os.getenv('programfiles', '') + '\\Thinkbox\\Deadline\\bin'
    elif os.name is 'posix':
        dlc = 'deadlinecommand'
        if sys.platform == 'darwin':
            #mac
            deadlineCommandPath = '/Applications/Thinkbox/Deadline10/Resources'
            deadlineBinPath = '/Users/Shared/Thinkbox/DEADLINE_PATH'
            if os.path.isfile(deadlineBinPath):
                with open(deadlineBinPath, 'r') as f:
                    deadlineBin = f.read()
                    deadlineCommandPath = deadlineBin.strip()

            if not os.path.isdir(deadlineCommandPath):
                deadlineCommandPath = '/Applications/Deadline/Resources/bin'
        else:
            #linux
            deadlineCommandPath = os.getenv('DEADLINE_PATH', '/opt/Thinkbox/Deadline10/bin')
            if not os.path.isdir(deadlineCommandPath):
                deadlineCommandPath = '/usr/local/Thinkbox/Deadline/bin'
    else:
        raise RuntimeError('Unknown operating system.  Unable to detect Deadline.')

    if not os.path.isdir(deadlineCommandPath):
        deadlineCommand = dlc
    else:
        deadlineCommand = deadlineCommandPath + os.sep + dlc

    if isinstance(command, str):
        command = [command]

    args = [deadlineCommand] + command

    print("// XMesh: Deadline command:", " ".join(args))

    try:
        proc = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, creationflags=creationflags)
        result, err = proc.communicate()
        proc.stdin.close()
        if proc.returncode != 0:
            raise DeadlineError(err)
    except OSError as e:
        if e.errno == errno.ENOENT:
            raise DeadlineNotFoundError()
        elif e.errno == errno.EINVAL:
            print("// XMesh: Error detecting Deadline:")
            traceback.print_exc()
            raise DeadlineInvokeError()
        else:
            raise

    return result

# Take the parameters specified in the saver window, bundle them into a file 'XMeshSaverParams.py'
# and initiate the Deadline script command, passing in the file as well.
def saveOverDeadline(xmeshSettings, deadlineSettings):
    print('// XMesh: Submitting job to Deadline...')

    outputPath = os.path.dirname(xmeshSettings.fullPath)
    projectPath = getProjectPath()

    # Handle Maya Scene File
    sceneFilePath = getSceneName()
    if deadlineSettings.p['IncludeSceneFile'] == True:
        submitMayaSceneFile = True
    else:
        deadlineSettings.p['SceneFile'] = sceneFilePath
        submitMayaSceneFile = False

    if len(maya.cmds.file(q=True, sn=True)) == 0:
        maya.cmds.confirmDialog(title='Scene Not Saved',
                                message='The current scene must be saved before being submitted to Deadline.',
                                button='OK')
        return

    # Detect if output folder exists
    if not os.path.isdir(outputPath):
        answer = maya.cmds.confirmDialog(   title='Create Directory?',
                                            message='The specified directory does not exist. Would you like to create it?',
                                            button=['Create', 'Cancel'],
                                            defaultButton='Create',
                                            cancelButton='Cancel',
                                            dismissString='Cancel',
                                            messageAlign='left')
        if answer == 'Create':
            os.makedirs(outputPath)
        else:
            return

    #error checking for Local Drive References and Missing Files
    message = ''
    if(not submitMayaSceneFile and isLocalDrive(sceneFilePath)):
        message = message + 'Maya scene file, \"' + sceneFilePath + '\" is on a local drive and is not being submitted.\nWorkers will not be able to access the scene file.\n\n'
    if not os.path.isdir(outputPath):
        message = message + 'Output Path, \"' + outputPath + '\" does not exist! The final files may be lost!\n\n'
    elif isLocalDrive(outputPath):
        message = message + 'Output Path, \"' + outputPath + '\" is on a local drive.\nWorkers will not be able to write files to this drive.\n\n'
    elif len(outputPath) == 0:
        message = message + 'The output path is blank! The final files will be lost!\n\n'

    # Display the error messages
    if len(message) > 0:
        message = message + '\nAre you sure you want to submit this job?'
        result = maya.cmds.confirmDialog(title='Confirm', message=(message), button=['Yes','No'], defaultButton='Yes', cancelButton='No', dismissString='No')
        if result == 'No':
            return

    # Save the scene if changes have been made.
    if maya.cmds.file(query=True, modified=True):
        print('// XMesh: Maya scene file has been modified, saving file')
        maya.cmds.file(save=True)

    #Temp Folder
    tempDir = maya.cmds.internalVar(userTmpDir=True)

    #Info Job
    submitInfoJob = checkSlashes(tempDir + 'maya_job_info.job')
    with open(submitInfoJob, 'w') as f:
        f.write(deadlineSettings.getDeadlineSettings())

    #Maya Plugin Job
    submitPluginJob = checkSlashes(tempDir + 'maya_plugin_info.job')
    with open(submitPluginJob, 'w') as f:
        f.write(deadlineSettings.getMayaPluginSettings())

    #Custom XMeshSaverParams.py
    XMeshSaverParamsFile = checkSlashes(tempDir + 'XMeshSaverParams.py')
    with open(XMeshSaverParamsFile, 'w') as f:
        f.write(xmeshSettings.pout('xmeshsettings'))

    # Job submission
    submissionCommand = [   checkSlashes(submitInfoJob),
                            checkSlashes(submitPluginJob),
                            checkSlashes(XMeshSaverParamsFile)
                        ]

    if submitMayaSceneFile:
        submissionCommand.insert(2, sceneFilePath)

    with waitCursor():
        try:
            submitResults = safeDeadlineCommand(submissionCommand)
            icon = ''
        except DeadlineError as e:
            submitResults = str(e)
            icon = 'critical'

    maya.cmds.confirmDialog(title='Submission Results', message=submitResults, button='OK', icon=icon)
    print("// XMesh: Deadline submission results:", submitResults)

def getMayaVersion():
    vers = maya.cmds.about(version=True)
    vers = vers.partition(' ')[0]
    return vers

def getSceneName():
    return maya.cmds.file(q=True, sn=True)

def getProjectPath():
    return maya.cmds.workspace(q=True, fn=True)
