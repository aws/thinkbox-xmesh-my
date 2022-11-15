# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

# XMesh Material Utils

import maya
import io
import os
import re
import string

try:
    # py3
    import configparser
except:
    # py2
    import ConfigParser as configparser

from io import StringIO

# This takes the incoming material name and sanitizes it to be a valid maya identifier
# This will potentially result in un-reversible translations when saving in maya.
# Currently this is unavoidable, however if non-modifying transformation through
# maya are required, perhaps a simple name-mangling system could be used
def cleanedMaterialName(materialName):
    # Remove all invalid identifier characters
    result = re.sub("[^A-Za-z0-9_]", "_", materialName.strip())
    # place an '_' in front of the first character if it is a digit
    if len(result) == 0 or result[0].isdigit():
        result = "_" + result
    return result

def readTextFileWithUnknownEncoding(filename):
    for encoding in ['utf-8', 'utf-16',  'latin-1']:
        try:
            with io.open(filename, "r", encoding=encoding) as f:
                lines = f.readlines()
                return lines
        except UnicodeError:
            pass
        except UnicodeDecodeError:
            pass
    return []

def readMaterialIDMapFile(filename):
    lines = readTextFileWithUnknownEncoding(filename)

    cfg = configparser.ConfigParser()
    cfg.readfp(StringIO(''.join(lines)))

    result = []
    if cfg.has_section('MaterialNames'):
        for (id,name) in cfg.items('MaterialNames'):
            id = id.strip()
            name = name.strip()
            if id.isdigit():
                result.append((int(id),name))
    return result

def replaceFrameNumber(path, newFrameNumber):
    (root,ext) = os.path.splitext(path)

    frameNumberCharacters = string.digits + '#'
    if root.endswith(','):
        root = root[:-1]
    else:
        if root.endswith(tuple(frameNumberCharacters)):
            root = root.rstrip(frameNumberCharacters)
        if len(root) > 2 and root.endswith(',') and root[-2] in frameNumberCharacters:
            root = root[:-1]
        root = root.rstrip(frameNumberCharacters)
    if root.endswith('-'):
        root = root[:-1]

    return root + newFrameNumber + ext

def getMaterialIDMapFilename(xmeshFilename):
    result = replaceFrameNumber(xmeshFilename, '')
    return os.path.splitext(result)[0] + '.matIDmap'

def isMesh(path):
    try:
        fnMesh = maya.OpenMaya.MFnMesh()
        fnMesh.setObject(path)
        return True
    except:
        return False

def getMeshDagPathsFromSelectionList(selectionList):
    result = []
    for selectionIndex in range(0,selectionList.length()):
        object = maya.OpenMaya.MObject()
        selectionList.getDependNode(selectionIndex, object)
        fnDagNode = maya.OpenMaya.MFnDagNode()
        fnDagNode.setObject(object)
        dagPath = maya.OpenMaya.MDagPath()
        fnDagNode.getPath(dagPath)
        numberOfShapesUtil = maya.OpenMaya.MScriptUtil()
        numberOfShapesUtil.createFromInt(0)
        numberOfShapesPtr = numberOfShapesUtil.asUintPtr()
        dagPath.numberOfShapesDirectlyBelow(numberOfShapesPtr)
        numberOfShapes = numberOfShapesUtil.getUint(numberOfShapesPtr)
        for i in range(0, numberOfShapes):
            childPath = maya.OpenMaya.MDagPath(dagPath)
            childPath.extendToShapeDirectlyBelow(i)
            if isMesh(childPath):
                result.append(childPath)
    return result

def getSelectedMeshDagPaths():
    selectionList = maya.OpenMaya.MSelectionList()
    maya.OpenMaya.MGlobal_getActiveSelectionList(selectionList)
    return getMeshDagPathsFromSelectionList(selectionList)

def getSurfaceShaderName(shadingEngine):
    dependencyNode = maya.OpenMaya.MFnDependencyNode(shadingEngine)
    plug = dependencyNode.findPlug('surfaceShader')
    plugArray = maya.OpenMaya.MPlugArray()
    plug.connectedTo(plugArray, True, False)
    if plugArray.length() > 0:
        surfaceShader = maya.OpenMaya.MFnDependencyNode(plugArray[0].node())
        return surfaceShader.name()
    return ''

class MaterialIDMap:
    def __init__(self):
        self.list = []
        self.materialToID = {}
        self.usedIDs = set()
        self.nextID = 0

    @classmethod
    def readFromFile(cls, path):
        result = cls()
        lines = readMaterialIDMapFile(path)
        for (id, name) in lines:
            result._addMaterialMapping(id, name, force=True)
        return result

    def writeToFile(self, path):
        cfg = configparser.ConfigParser()
        cfg.add_section('MaterialNames')
        for (id, name) in sorted(self.list):
            if len(name) == 0:
                name = 'undefined'
            cfg.set('MaterialNames', str(id), name)
        file = open(path, 'w')
        cfg.write(file)

    def _addMaterialMapping(self, id, name, force=False):
        internalName = name
        if internalName == 'undefined':
            internameName = ''

        if internalName not in self.materialToID:
            self.materialToID[internalName] = id
        self.usedIDs.add(id)
        if (id,name) not in self.list and (force or (id,cleanedMaterialName(name)) not in self.list):
            self.list.append((id,name))

    def _getNextMaterialID(self):
        while self.nextID in self.usedIDs:
            self.nextID += 1
        return self.nextID

    def _addMayaMaterial(self, name):
        if name in self.materialToID:
            return

        for otherName in self.materialToID:
            if cleanedMaterialName(otherName) == name:
                self._addMaterialMapping(self.materialToID[otherName], name)
                return

        self._addMaterialMapping(self._getNextMaterialID(), name)

    def addMaterialsFromSelectionList(self, selectionList):
        dagPaths = getMeshDagPathsFromSelectionList(selectionList)
        for dagPath in dagPaths:
            instanceNumber = 0
            isInstanced = dagPath.isInstanced()
            if isInstanced:
                instanceNumber = dagPath.instanceNumber()
            fnMesh = maya.OpenMaya.MFnMesh()
            fnMesh.setObject(dagPath)
            shaderArray = maya.OpenMaya.MObjectArray()
            shaderIndexArray = maya.OpenMaya.MIntArray()
            fnMesh.getConnectedShaders(instanceNumber, shaderArray, shaderIndexArray)
            for i in range(0, shaderArray.length()):
                shadingEngineNode = maya.OpenMaya.MFnDependencyNode(shaderArray[i])
                shadingEngineName = shadingEngineNode.name()
                if shadingEngineName not in self.materialToID:
                    self._addMayaMaterial(getSurfaceShaderName(shaderArray[i]))

    def addMaterialsFromSelection(self):
        selectionList = maya.OpenMaya.MSelectionList()
        maya.OpenMaya.MGlobal_getActiveSelectionList(selectionList)
        self.addMaterialsFromSelectionList(selectionList)

    def getIDList(self):
        return sorted(self.usedIDs)

    def getMaterialName(self, id):
        for (listID, listName) in self.list:
            if listID == id:
                return listName
        return None

    def getMaterialIDMapString(self):
        return ','.join(str(id) + '=' + str(name) for (name,id) in self.materialToID.items())
