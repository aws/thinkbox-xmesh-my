# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

# Scripted XMesh Sequence Saver for Maya
#

# Allows one to save multiple frames of elements to .xmesh format
# Since Maya requires .xmesh files to end in a frame number, the script
# will append frame numbers onto the given filename.

# This is the script run locally when the save operation is happening locally.

# Usage:
# import saveXMeshSequence
# mySettings = saveXMeshSequence.xmSettings()
# ....
# #assign settings (eg.  mySettings.fullPath = "/myproject/export/sequence1_.xmesh"
# ....
# saveXMeshSequence.XMeshSaverProc(mySettings)

from __future__ import print_function

import maya.cmds
import maya.mel
import math
import os.path
import string
import XMeshMaterialUtils
from contextlib import contextmanager
from distutils.version import StrictVersion

#Parameter Class
class xmSettings:
    xmVersion = '1.2'

    def __init__(self):
        self.version            = xmSettings.xmVersion
        self.fullPath           = ""
        self.suffix             = "xmesh"
        self.saveMain           = True
        self.saveProxy          = False
        self.proxyOptimization  = 'polyReduce'
        self.visibleOnly        = False
        self.worldSpace         = True
        self.combineObjects     = True
        self.startFrame         = 0
        self.endFrame           = 48
        self.frameStep          = 1.0
        self.objectList         = []
        self.subFolders         = False
        self.channelMask        = []
        self.channelProperties  = {}

        self.proxyChannelMask   = []
        self.proxyFrameStep     = 1.0
        self.proxyPercent       = 10.0

        self.useDeadlineOutputDirectory = False
        self.deadlineTaskCount  = None

    def __repr__(self):
        return os.linesep.join('%s=%s' % (k, v) for (k, v) in self.__dict__.items())

    #output python compilable instance
    def pout(self, out):
        lines = '\n'.join('%s.%s = %s' % (out, k, repr(v)) for (k, v) in self.__dict__.items())
        items = [   'print("XMesh: start saving...")',
                    '',
                    'import saveXMeshSequence',
                    '',
                    '%s = saveXMeshSequence.xmSettings()' % out,
                    '',
                    lines,
                    '',
                    'proc = saveXMeshSequence.XMeshSaverProc(%s)' % out,
                    '',
                    'print("XMesh: done saving")'
                ]

        return os.linesep.join(items)

    def upgrade_1_to_1_2(self):
        self.saveProxy = self.useProxy
        self.proxyOptimization = self.proxySampling
        self.frameStep = 1.0 / self.subFrames
        self.proxyFrameStep = 1.0 / self.proxySubFrames

        del self.useProxy
        del self.vertsOnly
        del self.worldspace
        del self.proxySampling
        del self.subFrames
        del self.proxySubFrames
        del self.channelMaskType
        del self.proxyMinVerts

        self.version = '1.2'

    def upgrade(self):
        if StrictVersion(self.version) == StrictVersion('1.0'):
            self.upgrade_1_to_1_2()

#Control Prodedure
class XMeshSaverProc:
    def __init__(self, settings=None):
        if settings is None:
            self.s = None
            print("// XMesh: No Settings Found")
        else:
            if settings.version == 1:
                settings.version = "1.0"

            if StrictVersion(settings.version) > StrictVersion(xmSettings.xmVersion):
                maya.cmds.error("XMesh version %s is outdated.  Version required: %s"  % (str(xmSettings.xmVersion), str(settings.version)))
            else:
                settings.upgrade()
                if settings.useDeadlineOutputDirectory:
                    outputDirectory = maya.mel.eval('DeadlineValue "OutputDirectory0"')
                    if len(outputDirectory) > 0:
                        settings.fullPath = outputDirectory + '/' + os.path.basename(settings.fullPath)

                settings.frameStep = getNearestValidFrameStepTime(settings.frameStep).asUnits(maya.OpenMaya.MTime.uiUnit())
                settings.proxyFrameStep = getNearestValidFrameStepTime(settings.proxyFrameStep).asUnits(maya.OpenMaya.MTime.uiUnit())

                self.s = settings

                self.inProxyMode = False
                self.reduceNodes = []

                if self.s.deadlineTaskCount is None:
                    task = 0
                    taskCount = 1
                else:
                    task = int(maya.mel.eval('DeadlineValue "StartFrame"'))
                    taskCount = self.s.deadlineTaskCount
                (self.startFrame, self.endFrame, self.saveMain, self.saveProxy) = getTaskParameters(self.s.startFrame, self.s.endFrame, self.s.frameStep, self.s.saveProxy, task, taskCount)

                self.frameStep = self.s.frameStep

                #save
                self.saveFrames()

    @contextmanager
    def renderMode(self):
        self.toggleRenderMode(True)
        try:
            yield
        finally:
            self.toggleRenderMode(False)

    def toggleRenderMode(self, mode):
        if mode:
            script = maya.cmds.getAttr("defaultRenderGlobals.preMel")
        else:
            script = maya.cmds.getAttr("defaultRenderGlobals.postMel")

        if script is not None:
            maya.mel.eval(script)

    @contextmanager
    def proxyMode(self):
        self.toggleProxyMode(True)
        try:
            yield
        finally:
            self.toggleProxyMode(False)

    def toggleProxyMode(self,mode):
        _stCH = maya.cmds.constructionHistory(q=True, tgl=True)
        #Turn On constructionHistory
        maya.cmds.constructionHistory(tgl=True)
        if mode:
            self.inProxyMode = True
            self.frameStep = float(self.s.proxyFrameStep)
            self.reduceNodes = []
            if self.s.proxyOptimization == '':
                pass
            elif self.s.proxyOptimization == 'polyReduce':
                for i in self.s.objectList:
                    tmp = maya.cmds.polyReduce(i, percentage=(100.0 - self.s.proxyPercent))
                    self.reduceNodes.append(tmp)
            else:
                maya.cmds.error('Unknown Proxy Optimization mode: ' + str(self.s.proxyOptimization))
        else:
            self.inProxyMode = False
            self.frameStep = float(self.s.frameStep)
            for i in self.reduceNodes:
                maya.cmds.delete(i)
            self.reduceNodes = []
        #Reset the constructionHistory
        maya.cmds.constructionHistory(tgl=_stCH)

    # manages saving of all necessary file sequences
    def saveFrames(self):
        self.s.objectList.sort()
        itemsToSave =   self.s.objectList
        filePath =      self.s.fullPath

        #remember current frame
        initialTime = maya.cmds.currentTime(query=True)

        if itemsToSave is None or len(itemsToSave) == 0:
            maya.cmds.warning("No objects selected.")
        elif len(filePath) == 0:
            maya.cmds.warning("No pathname specified.")
        elif not (self.saveMain or self.saveProxy):
            maya.cmds.warning("No sequence will be saved, because saving is disabled for both the main sequence and the proxy sequence.")
        else:
            # Prompt to create new directory if specified base path does not exist
            if not os.path.isdir(os.path.dirname(filePath)):
                raise RuntimeError('The specified save directory "' + os.path.dirname(filePath) + '" does not exist')

            # Use .xmesh suffix by default
            if len(os.path.splitext(filePath)[1]) == 0:
                filePath += self.s.suffix

            #remove missing items
            for i in itemsToSave:
                if not maya.cmds.objExists(i):
                    maya.cmds.warning("No object in the scene exists with the name: " + i)
                    self.s.objectList.remove(i)

            #progressBar
            try:
                #single object (Combined)
                if self.s.combineObjects:
                    if self.saveMain:
                        with self.renderMode():
                            self.procCombined()
                    if self.saveProxy:
                        with self.proxyMode():
                            self.procCombined()

                #individual objects
                else:
                    if self.saveMain:
                        with self.renderMode():
                            self.procIndividual()
                    if self.saveProxy:
                        with self.proxyMode():
                            self.procIndividual()
            finally:
                #restore current time
                maya.cmds.currentTime(initialTime)

    def saveXMeshLoaderCreators(self, pathList):
        for aPath in pathList:
            saveXMeshLoaderCreatorMEL(aPath, self.s.startFrame, self.s.endFrame)
            saveXMeshLoaderCreatorMS(aPath, self.s.startFrame, self.s.endFrame)

    # EXAMPLE
    # input: c:\path\f.xmesh obj1

    # full
    #               txtF:
    #   ind     :
    #               c:\path\f\obj1_0000.xmesh
    #   indSub  :
    #               c:\path\f\obj1\obj1_0000.xmesh
    #   combine :
    #               c:\path\f_0000.xmesh
    # proxy
    #   ind     :
    #               c:\path\f\obj1_0000.xmesh
    #               c:\path\f\obj1_proxy\obj1_proxy_0000.xmesh
    #   indSub      :
    #               c:\path\f\obj1\obj1_0000.xmesh
    #               c:\path\f\obj1\obj1_proxy\obj1_proxy_0000.xmesh
    #   combine :
    #               c:\path\f_0000.xmesh
    #               c:\path\f_proxy\f_proxy_0000.xmesh

    def procCombined(self):
        if self.inProxyMode:
            cm = self.s.proxyChannelMask
            arg = getProxyPath(self.s.fullPath)
            createMissingDirectory(os.path.dirname(arg))
        else:
            cm = self.s.channelMask
            arg = self.s.fullPath

        savePerSequenceFiles = (self.s.startFrame == self.startFrame)

        #save
        saveFiles([arg], [self.s.objectList], self.s.worldSpace, cm, self.s.channelProperties, self.startFrame, self.endFrame, self.frameStep, self.s.visibleOnly, savePerSequenceFiles)

        if savePerSequenceFiles:
            self.saveXMeshLoaderCreators([arg])

    def procIndividual(self):
        #split filename/extension
        sPath,sFile,sExt = splitFilename(self.s.fullPath)

        sRoot = sPath

        createMissingDirectory(sRoot) #common root or not?
        if not self.s.subFolders:
            createMissingDirectory(sRoot)

        pathList = []
        objectListList = []

        for objectName in self.s.objectList:
            i = cleanName(objectName)

            if self.s.subFolders:
                createMissingDirectory(sRoot + i)
                arg = sRoot+    i+'/'+  i+'_'   +sExt
            else:
                arg = sRoot+            i+'_'   +sExt

            if self.inProxyMode:
                arg = getProxyPath(arg)
                createMissingDirectory(os.path.dirname(arg))
                cm = self.s.proxyChannelMask
            else:
                cm = self.s.channelMask

            pathList.append(arg)
            objectListList.append([objectName])

        savePerSequenceFiles = (self.s.startFrame == self.startFrame)

        #save
        saveFiles(pathList, objectListList, self.s.worldSpace, cm, self.s.channelProperties, self.startFrame, self.endFrame, self.frameStep, self.s.visibleOnly, savePerSequenceFiles)

        if savePerSequenceFiles:
            self.saveXMeshLoaderCreators(pathList)


def getTaskFrameRange(numFrames, numTasks, taskNum, frameStep, startFrame, endFrame):

    remainder = numFrames % numTasks
    taskFrames = numFrames // numTasks

    taskFramesToAdd = taskFrames - 1

    if taskNum == 0 or remainder == 0:
        partFramesBefore = 0
    else:
        partFramesBefore = min(remainder, taskNum)

    taskStartFrame = ((taskFramesToAdd * taskNum) + partFramesBefore + taskNum) * frameStep
    taskFramesToAdd += 1 if taskNum < remainder else 0
    taskEndFrame = taskStartFrame + (taskFramesToAdd * frameStep)

    taskStartFrame += startFrame
    taskEndFrame += startFrame

    taskStartFrame = min(endFrame, taskStartFrame)
    taskEndFrame = min(endFrame, taskEndFrame)

    taskSaveRender = True

    return (taskStartFrame, taskEndFrame)


def getTaskParameters(startFrame, endFrame, frameStep, saveProxy, taskNum, taskCount):

    if startFrame > endFrame:
        raise Exception("The starting frame cannot be after the ending frame.")
    if taskCount <= taskNum or taskNum < 0:
        raise Exception("taskNum must be positive and less than taskCount.")
    if frameStep <= 0:
        raise Exception("frameStep cannot be less than or equal to 0.")
    if taskCount < 1:
        raise Exception("taskCount cannot be less than 1.")

    if saveProxy:
        proxyTasks = int(taskCount/2)
        taskSaveProxy = True if taskNum < proxyTasks and saveProxy else False
    else:
        proxyTasks = 0
        taskSaveProxy = False

    renderTasks = int(taskCount) - proxyTasks
    renderTaskNum = taskNum - proxyTasks

    numFrames = math.fabs(endFrame - startFrame) + 1
    numStepFrames = int(math.ceil((numFrames -1)/frameStep) + 1 )

    if (taskSaveProxy and taskNum >= numStepFrames ) or (not taskSaveProxy and renderTaskNum >= numStepFrames ):
        taskStartFrame = 0
        taskEndFrame = 0
        taskSaveProxy = False
        taskSaveRender = False

    elif taskCount > 1:
        if taskSaveProxy:
            taskStartFrame, taskEndFrame = getTaskFrameRange(numStepFrames, proxyTasks, taskNum, frameStep, startFrame, endFrame)
            taskSaveRender = False

        else:
            taskStartFrame, taskEndFrame = getTaskFrameRange(numStepFrames, renderTasks, renderTaskNum, frameStep, startFrame, endFrame)
            taskSaveRender = True

    elif taskCount == 1:

        taskStartFrame = startFrame
        taskEndFrame = endFrame
        taskSaveProxy = saveProxy
        taskSaveRender = True


    return (taskStartFrame, taskEndFrame, taskSaveRender, taskSaveProxy)


def getValidSamplingSteps():
    result = []
    tickUnit = maya.OpenMaya.MTime.k6000FPS
    ticksPerFrame = int(maya.OpenMaya.MTime(1.0, maya.OpenMaya.MTime.uiUnit()).asUnits(tickUnit))
    for i in range(1, ticksPerFrame+1):
        if float(ticksPerFrame/i) == float(ticksPerFrame)/i:
            result.append(maya.OpenMaya.MTime(float(i), tickUnit))
    for step in [2, 5, 10]:
        result.append(maya.OpenMaya.MTime(float(step), maya.OpenMaya.MTime.uiUnit()))
    return result

def getNearestValidFrameStepTime(frameStep):
    validFrameStep = min(getValidSamplingSteps(), key=lambda x: abs(x.asUnits(maya.OpenMaya.MTime.uiUnit()) - frameStep))
    return validFrameStep

def getProxyPath(path):
    sPath,sFile,sExt = splitFilename(path)
    return sPath + sFile + "_proxy/"  + sFile + "_proxy" + sExt


def saveFiles(pathList, objectListList, worldSpace, channelMask, channelProperties, minFrame, maxFrame, frameStep, visibleOnly, saveMaterialIDMap):
    if len(pathList) != len(objectListList):
        raise Exception("Mismatch between length of pathList and length of objectListList")

    step = getNearestValidFrameStepTime(frameStep)

    materialIDMapList = []
    for (path,objectList) in zip(pathList,objectListList):
        if 'MaterialID' in channelMask:
            matIDMapPath = XMeshMaterialUtils.getMaterialIDMapFilename(path)
            if os.path.exists(matIDMapPath):
                matIDMap = XMeshMaterialUtils.MaterialIDMap.readFromFile(matIDMapPath)
            else:
                matIDMap = XMeshMaterialUtils.MaterialIDMap()
            selectionList = maya.OpenMaya.MSelectionList()
            for i in objectList:
                selectionList.add(i)
            matIDMap.addMaterialsFromSelectionList(selectionList)
            if saveMaterialIDMap:
                matIDMap.writeToFile(matIDMapPath)
        else:
            matIDMap = XMeshMaterialUtils.MaterialIDMap()
        materialIDMapList.append(matIDMap.getMaterialIDMapString())

    channelMap = []
    for ch in channelMask:
        if ch in channelProperties:
            channelMap.append(ch+'='+channelProperties[ch])
        else:
            channelMap.append(ch)

    maya.cmds.saveXMeshSequence(path=pathList,
                                objects=[','.join(objectList) for objectList in objectListList],
                                worldSpace=worldSpace,
                                frameRange=(minFrame, maxFrame),
                                step=step.asUnits(maya.OpenMaya.MTime.uiUnit()),
                                materialIDMap=materialIDMapList,
                                channelMap=','.join(channelMap),
                                visibleOnly=visibleOnly)

def splitFilename(sFile):

    #todo use os.path
    fullpath        = sFile.replace('\\','/')
    parts           = fullpath.split('/')
    filenameExt     = parts.pop()
    parts2          = filenameExt.split('.')
    ext             = '.' + parts2.pop()
    file            = '.'.join(parts2)
    path            = '/'.join(parts) + '/'

    return (path, file, ext)

def createMissingDirectory(dirStructure):
    if not os.path.isdir(dirStructure):
        os.mkdir(dirStructure)

def cleanName(name):
    name = name.replace("|","_")
    name = name.replace(":",".")
    return name

def saveXMeshLoaderCreatorMEL(theXmeshFile, minFrame, maxFrame): #this creates a MEL script to create an XMesh Loader in Maya
    sPath,sFile,sExt = splitFilename(theXmeshFile)
    melPath = sPath+sFile+".MEL"
    xmeshPath = sPath+sFile+"0001"+sExt
    escapedXMeshPath = ''.join(('\\' + c) if c in '\\\'"' else c for c in xmeshPath)
    with open(melPath, 'w') as f:
        f.write("//Exported from Autodesk Maya\n")
        pythonCommand = "import createXMeshLoader; reload(createXMeshLoader); createXMeshLoader.createXMeshLoaderFromPath('"+escapedXMeshPath+"');"
        f.write('python("' + maya.cmds.encodeString(pythonCommand) + '");\n')
        #f.write("string $sel[] = `ls -sl`;\n")
        #f.write("string $xmloader = `substitute \"Transform\" $sel[0] \"\"`;\n")

def saveXMeshLoaderCreatorMS(theXmeshFile, minFrame, maxFrame): #this creates a MAXScript file to create an XMesh Loader in 3ds Max
    sPath,sFile,sExt = splitFilename(theXmeshFile)
    msPath = sPath+sFile+".MS"
    xmeshPath = sPath+sFile+"0001"+sExt
    xmeshName = sFile+"0001"+sExt
    with open(msPath, 'w') as f:
        f.write("--Exported from Autodesk Maya--\n")
        f.write("(\nlocal theXMeshLoader = XMeshLoader()\n")
        f.write("local thePath = getFilenamePath (getThisScriptFilename())\n")
        f.write("local goOn = true\n")
        f.write("if not doesFileExist (thePath+\"\\\\\"+\""+xmeshName+"\" ) do (thePath = @\""+sPath+"\")\n")
        f.write("if not doesFileExist (thePath+\"\\\\\"+\""+xmeshName+"\" ) do ((messagebox \"Please ensure you are executing the script from a MAPPED PATH or local drive to automatically resolve the path.\n\n If you are executing from a Network location, make sure the hard-coded path in the script exists.\" title:\"XMesh Source Sequence Path Not Found\"); goOn = false) \n" )
        f.write("if goOn == true do (\n")
        f.write("theXMeshLoader.renderSequence = thePath + \""+xmeshName+"\" \n")
        f.write("theXMeshLoader.rangeFirstFrame = "+str(minFrame)+"\n")
        f.write("theXMeshLoader.rangeLastFrame = "+str(maxFrame)+"\n")
        f.write("theXMeshLoader.limitToRange = true\n")

        f.write("local theXMeshLayer = LayerManager.getLayerFromName  \"XMesh Loaders\" \n")
        f.write("if theXMeshLayer == undefined do theXMeshLayer = LayerManager.newLayerFromName \"XMesh Loaders\" \n")
        f.write("theXMeshLayer.addnode theXMeshLoader \n")
        f.write("theXMeshLoader.viewportSequenceID = 0 \n")
        f.write("theXMeshLoader.name = uniquename \""+sFile+"_\" \n")
        f.write("select theXMeshLoader \n")
        f.write(")\n)\n")