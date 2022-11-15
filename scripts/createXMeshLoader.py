# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

# Scripted utilities for Maya XMesh Loader

import maya.cmds
import os.path
import re
import io
import string
import platform
import XMeshMaterialUtils

def createXMeshLoader():
    if not maya.cmds.pluginInfo('XMesh', query=True, loaded=True):
        if platform.system() in ['Linux', 'Darwin']:
            # avoid calling loadPlugin for now due to crashes observed on these platforms
            maya.cmds.confirmDialog(title='XMesh', m='XMesh is not loaded.\n\nPlease ensure that XMesh is loaded by using the Plug-in Manager.\n\nYou can access the Plug-in Manager by using the Window > Settings/Preferences > Plug-in Manager menu item.')
            return
        else:
            results = maya.cmds.loadPlugin('XMesh')
            if results is None or len(results) == 0:
                raise Exception('Could not load XMesh plugin.  Cannot proceed.')
                return

    xmeshFilter = 'XMesh Mel Scenes, XMesh Files (*.mel *.xmesh);;XMesh Mel Scenes (*.mel);;XMesh Files (*.xmesh)'

    maya.mel.eval('source XMeshLoadDialog.mel')
    path = maya.cmds.fileDialog2(optionsUICreate='xmeshLoadDialog_setupParams', optionsUICommit='xmeshLoadDialog_commitParams', fileFilter=xmeshFilter, dialogStyle=2, fileMode=1)

    displayPercentage = maya.mel.eval('$tempVal = $xmesh_displayPercent')
    displayStyle = maya.mel.eval('$tempVal = $xmesh_displayStyle')

    if path is not None and len(path) > 0:
        ext = os.path.splitext(path[0])[1].lower()
        if ext == '.mel':
            fn = path[0]
            with open(fn, 'r') as f:
                mel_data = f.read()
                maya.mel.eval(mel_data)
        else:
            createXMeshLoaderSub(path[0], displayStyle=displayStyle, displayPercentage=displayPercentage)
            # the file dialog returns an array of strings,
            # the first of which is the filepath of the selected file

def createXMeshLoaderFromPath(path):
    if not maya.cmds.pluginInfo('XMesh', query=True, loaded=True):
        if platform.system() in ['Linux', 'Darwin']:
            # avoid calling loadPlugin for now due to crashes observed on these platforms
            maya.cmds.confirmDialog(title='XMesh', m='XMesh is not loaded.\n\nPlease ensure that XMesh is loaded by using the Plug-in Manager.\n\nYou can access the Plug-in Manager by using the Window > Settings/Preferences > Plug-in Manager menu item.')
            return
        else:
            results = maya.cmds.loadPlugin('XMesh')
            if results is None or len(results) == 0:
                raise Exception('Could not load XMesh plugin.  Cannot proceed.')
                return

    createXMeshLoaderSub(path)

# All of this is the original script required to create the graph in Maya
def createXMeshLoaderSub(fileName, **kwargs):
    displayStyle = None
    displayPercentage = None
    for (key, value) in kwargs.items():
        if key == 'displayStyle':
            displayStyle = value
        elif key == 'displayPercentage':
            displayPercentage = value
        else:
            raise TypeError("createXMeshLoaderSub() got an unexpected keyword argument '" + key + "'")

    baseName = 'xmesh_' + stripFrameNumber(os.path.splitext(os.path.split(fileName)[1])[0])
    xmeshTransform = maya.cmds.createNode('transform', name=baseName+"Transform#")
    xmeshShape = maya.cmds.createNode('mesh', parent=xmeshTransform, name=baseName+"Shape#")
    maya.cmds.setAttr(xmeshShape+'.motionVectorColorSet', 'velocityPV', type='string')
    xmesh = maya.cmds.createNode('sequenceXMesh', parent=xmeshTransform, name=baseName+"#")
    maya.cmds.connectAttr('time1.outTime',xmesh+'.inTime')
    maya.cmds.connectAttr(xmesh+'.outMesh',xmeshShape+'.inMesh')
    if displayStyle is not None:
        maya.cmds.setAttr(xmesh + '.inDisplayStyle', displayStyle )
    if displayPercentage is not None:
        maya.cmds.setAttr(xmesh + '.inDisplayPercent', displayPercentage )
    loadXMeshSequence(fileName, xmesh)
    maya.cmds.select(xmeshShape)
    maya.cmds.polyColorSet(query=True, allColorSets=True)
    maya.cmds.select(xmesh)

def sequenceXMeshGetMeshNode(xmeshNodeName):
    meshNodeAttr = maya.cmds.connectionInfo(xmeshNodeName + ".outMesh", destinationFromSource=True)
    if len(meshNodeAttr) > 0:
        return meshNodeAttr[0].split(".")[0]
    else:
        return None

# This locates the name of an existing shading group based on the
# name of an existing shader.  Note that this will grab only the one
# such shading group (multiple shading groups can be attached to a single shader)
def shadingGroupFromShader(shaderName):
    shadingGroups = maya.cmds.listConnections(shaderName + ".outColor")
    if shadingGroups != None and len(shadingGroups) > 0:
        return shadingGroups[-1]
    else:
        return None

# This method will remove _all_ shaders and clean up _all_ groupId nodes attached to any given mesh node
# please call this before trying to assign materials to an xmesh node
def disconnectAllShaders(xmeshShapeNodeName):
    destinationConnections = maya.cmds.listConnections(xmeshShapeNodeName, type="shadingEngine", destination=True, source=False, connections=True, plugs=True)
    if destinationConnections != None:
        for i in range(0, len(destinationConnections), 2):
            maya.cmds.disconnectAttr(destinationConnections[i], destinationConnections[i+1])
    sourceConnections = maya.cmds.listConnections(xmeshShapeNodeName, type="shadingEngine", destination=False, source=True, connections=True, plugs=True)
    if sourceConnections != None:
        for i in range(0, len(sourceConnections), 2):
            maya.cmds.disconnectAttr(sourceConnections[i+1], sourceConnections[i])
    groupNodes = maya.cmds.listConnections(xmeshShapeNodeName, type="groupId", destination=False, source=True, connections=False, plugs=False)
    if groupNodes != None:
        for node in groupNodes:
            maya.cmds.delete(node)

def looksLikeSingleFile(filename):
    root = os.path.splitext(filename)[0]
    if len(root) > 0 and root[-1].isdigit():
        return False
    return True

def stripFrameNumber(s):
    gotDecimalPoint = False
    while len(s):
        if s[-1].isdigit():
            s = s.rstrip(string.digits)
        elif s[-1] == '#':
            s = s.rstrip('#')
        elif s[-1] == '-':
            return s[:-1]
        elif s[-1] == ',':
            if gotDecimalPoint:
                return s
            else:
                gotDecimalPoint = True
                s = s[:-1]
        else:
            return s

def addXMeshDefaultRenderGlobalsCommands():
    def addCommand(attribute, command):
        s = maya.cmds.getAttr('defaultRenderGlobals.' + attribute)
        if s is None:
            s = ''
        if len(s) > 0 and re.match('.*[};][ \t\n\r]*$', s) is None:
            s += ';'
        if s.find(command) == -1:
            s += 'if(`exists ' + command + '`){' + command + ';}'
            maya.cmds.setAttr('defaultRenderGlobals.' + attribute, s, type='string')
    addCommand('preMel', 'xmeshPreRender')
    addCommand('postMel', 'xmeshPostRender')

def loadXMeshSequence(fileName, xmeshNodeName):
    maya.cmds.setAttr(xmeshNodeName+'.seqPath', fileName, type='string')

    if looksLikeSingleFile(fileName):
        maya.cmds.setAttr(xmeshNodeName+'.inLoadingMode', 0)
    else:
        syncXMeshFrameRange(xmeshNodeName)

    addXMeshDefaultRenderGlobalsCommands()

    #Material Assignment
    connectedShader = False
    xmeshShapeNodeName = sequenceXMeshGetMeshNode(xmeshNodeName)

    materialIDFilename = XMeshMaterialUtils.getMaterialIDMapFilename(fileName)
    if os.path.exists(materialIDFilename):
        # we have to disconnect any existing shaders before assigning any new ones, otherwise things will not work out very nicely
        disconnectAllShaders(xmeshShapeNodeName)
        shadingEngineList = maya.cmds.ls(type="shadingEngine")
        materialsList = maya.cmds.ls(materials=True)
        
        groupNumber = 0
        materialIDMap = XMeshMaterialUtils.MaterialIDMap.readFromFile(materialIDFilename)
        for materialID in materialIDMap.getIDList():
            materialName = materialIDMap.getMaterialName(materialID)

            if materialName is None or materialName == '' or materialName == 'undefined':
                continue

            if materialName not in shadingEngineList and materialName not in materialsList:
                materialName = XMeshMaterialUtils.cleanedMaterialName(materialName)

            materialNameSG = materialName + 'SG'

            shadingGroupName = None
            materialExists = materialName in materialsList
            if materialName in shadingEngineList:
                shadingGroupName = materialName
            elif materialNameSG in shadingEngineList:
                shadingGroupName = materialNameSG
            elif materialExists:
                shadingGroupName = shadingGroupFromShader(materialName)

            # if no shading group could be found, create a default one and attatch this shader to it
            # TODO: check if more than one default material needs to be made.
            if shadingGroupName is None:
                shadingGroupName = maya.cmds.sets(renderable=True, empty=True, noSurfaceShader=True, name=materialNameSG)
                if shadingGroupName != materialNameSG:
                    shadingGroupName = maya.cmds.rename(shadingGroupName, xmeshNodeName + "MaterialSet_" + materialName)

                # update shadingEngineList to include the shader we just added
                shadingEngineList = maya.cmds.ls(type="shadingEngine")

                if materialExists:
                    maya.cmds.connectAttr(materialName+".outColor", shadingGroupName+".surfaceShader")
                else:
                    defaultSurfaceShader = maya.cmds.connectionInfo('initialShadingGroup.surfaceShader', sourceFromDestination=True)
                    if defaultSurfaceShader:
                        maya.cmds.connectAttr(defaultSurfaceShader, shadingGroupName+".surfaceShader")

            groupNode = maya.cmds.createNode("groupId")
            if xmeshShapeNodeName is not None:
                maya.cmds.connectAttr(groupNode+".groupId", xmeshNodeName+".groupIds["+str(materialID)+"]", force=True)
                maya.cmds.connectAttr(groupNode+".message", shadingGroupName+".groupNodes", nextAvailable=True, force=True)
                maya.cmds.connectAttr(xmeshShapeNodeName+".instObjGroups.objectGroups["+str(groupNumber)+"]", shadingGroupName+".dagSetMembers", nextAvailable=True, force=True)
                maya.cmds.connectAttr(groupNode+".groupId", xmeshShapeNodeName+".instObjGroups.objectGroups["+str(groupNumber)+"].objectGroupId", force=True)
                maya.cmds.connectAttr(shadingGroupName+".memberWireframeColor", xmeshShapeNodeName+".instObjGroups.objectGroups["+str(groupNumber)+"].objectGrpColor", force=True)
            connectedShader = True
            groupNumber += 1

    if not connectedShader:
        maya.cmds.sets(xmeshShapeNodeName, edit=True, forceElement="initialShadingGroup")

def syncXMeshFrameRange(xmeshNodeName):
    minFile = maya.cmds.getAttr(xmeshNodeName + ".outMinimumAvailableFileIndex")
    maxFile = maya.cmds.getAttr(xmeshNodeName + ".outMaximumAvailableFileIndex")
    maya.cmds.setAttr(xmeshNodeName + ".inUseCustomRange", True)
    maya.cmds.setAttr(xmeshNodeName + ".inCustomRangeStart", minFile)
    maya.cmds.setAttr(xmeshNodeName + ".inCustomRangeEnd", maxFile)
