# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

# Scripted UI for Maya XMesh Saver

from __future__ import print_function

import maya.cmds
import maya.mel
import math
import os.path
import copy
import re
from functools import partial

try:
    import itertools.izip as zip
except ImportError:
    pass

import XMeshDeadlineUtils as DL

from XMeshUtils import *

import saveXMeshSequence
import XMeshNoticesDialog

gUseDeadline = False
gDLInvokeError = False
try:
    gUseDeadline = DL.isInstalled()
except DL.DeadlineInvokeError:
    gDLInvokeError = True
    pass

xmChannels = [
              'Velocity',
              'SmoothingGroup',
              'Normal',
              'Color',
              'TextureCoord',
              ','.join('Mapping'+str(i) for i in range(2,100)),
              'MaterialID',
              'EdgeSharpness,VertexSharpness'
              ]

defaultXMChannels = set(xmChannels)
defaultXMChannels.remove('Normal')
try:
    defaultXMChannels.remove('Color')
except KeyError:
    pass

xmToUIChannel = {
        'SmoothingGroup' : 'Edge Smoothing',
        'Color' : 'color',
        'TextureCoord': 'map1'
    }

xmUIChannels = copy.deepcopy(xmChannels)
for i in range(0, len(xmUIChannels)):
    if xmUIChannels[i] in xmToUIChannel:
        xmUIChannels[i] = xmToUIChannel[xmUIChannels[i]]
    if xmUIChannels[i].startswith('Mapping2'):
        xmUIChannels[i] = 'map2..map99'
    if xmUIChannels[i].startswith('EdgeSharpness'):
        xmUIChannels[i] = 'Creases'

loggingLevels = [
            'Nothing',
            'Errors',
            'Warnings',
            'Progress',
            'Stats',
            'Debug'
            ]

_s = {
    "title"                             : "XMesh Saver",
    "ObjLabel"                          : "Objects To Save",
    "SelectAll"                         : "Highlight All",
    "SelectInverse"                     : "Highlight Inverse",
    "SelectNone"                        : "Highlight None",
    "AddObjects"                        : "ADD Selected Objects To List",
    "RemoveObjects"                     : "REMOVE Highlighted Items From List",
    "AddChildShapes"                    : "Add Child Shapes Of Selected To List",
    "BasePathLabel"                     : "Base Path:",
    "RevisionLabel"                     : "Revision:",
    "LastRevisionCmd"                   : "Set to Last Revision",
    "NextRevisionCmd"                   : "Set to Next Revision (Increase By 1)",
    "ClearRevisionCmd"                  : "Clear Revision Field",
    "SeqLabel"                          : "Name:",
    "OutputFrameLabel"                  : "Output Path",
    "MeshMode_World_Single"             : "Save ALL Objects As ONE MESH In WORLD SPACE Coordinates",
    "MeshMode_Object_Separate"          : "Save EACH Object To INDIVIDUAL MESH In OBJECT SPACE Coordinates",
    "MeshMode_World_Separate"           : "Save EACH Object To INDIVIDUAL MESH In WORLD SPACE Coordinates",
    "MakeSubFolders"                    : "Make Subfolder For Each Object",
    "PreviewLabel"                      : "Final Output Path:",
    "FrameRangeLabel"                   : "Frames To Save",
    "FrameStartLabel"                   : "Start Frame:",
    "FrameEndLabel"                     : "End Frame:",
    "FrameSamples"                      : "Samples Per Frame:",
    "ChannelFrameLabel"                 : "Channels To Save",
    "ProxyFrameLabel"                   : "Proxy Settings",
    "SaveProxy"                         : "Save Proxy Sequence",
    "UseProxySampling"                  : "Use Proxy Sampling",
    "Percent"                           : "To Percent:",
    "saveBtn"                           : "SAVE XMESH SEQUENCE TO DISK",
    "closeBtn"                          : "Close",
    "SetFrameRangeLabel"                : "Set To Range:",
    "PreferencesLabel"                  : "Advanced Settings",
    "FrameRangeScene"                   : "Animation Range",
    "FrameRangePlayback"                : "Playback Range",
    "FrameRangeRender"                  : "Render Range",
    "FramgeRangeCurrent"                : "Current Frame",
    "LoggingLevelLabel"                 : "Logging Level:"
}

proxyOptimizationSettings = [('Reduce', 'polyReduce'), ('Original', '')]
proxyOptimizationLabels = [label for (label, mode) in proxyOptimizationSettings]
proxyOptimizationModeToLabel = dict((mode, label) for (label, mode) in proxyOptimizationSettings)
proxyOptimizationLabelToMode = dict((label, mode) for (label, mode) in proxyOptimizationSettings)

#control widgets
class controlWidgets:
    pass

_w = controlWidgets()
_w.windo                = None
_w.scrollFrame          = None
_w.objList              = None #
_w.selectAll            = None
_w.selectInverse        = None
_w.selectNone           = None
_w.add                  = None
_w.rem                  = None

_w.fPath                = None #
_w.fRevision            = None #
_w.fProjectName         = None #
_w.fExtension           = None #
_w.pathPreview          = None

_w.clearRevision        = None
_w.nextRevision         = None
_w.lastRevision         = None
_w.pathFrame            = None #
_w.pathFrameBG          = None
_w.frameFrame           = None
_w.channelFrame         = None #
_w.channelType          = None
_w.channelList          = None
_w.colorFromCurrentColorSet = None
_w.map1FromCurrentUVSet = None
_w.meshMode             = None #
_w.subFolders           = None #
_w.startFrame           = None #
_w.endFrame             = None #
_w.subFrames            = None #
_w.useProxy             = None #
_w.useProxySampling     = None #
_w.proxyOptimization    = None #
_w.proxySubFrames       = None #
_w.proxyPercent         = None #
_w.proxyFrame           = None #
_w.proxyFrameBG         = None
_w.preferencesFrame     = None #
_w.loggingLevel         = None
_w.visibleOnly          = None


class deadlineControlWidgets:
    pass

_wDL = deadlineControlWidgets()

_wDL.frame              = None
_wDL.enable             = None

_wDL.jobName            = None
_wDL.comment            = None
_wDL.department         = None
_wDL.pool               = None
_wDL.group              = None
_wDL.machineLimit       = None
_wDL.priority           = None
_wDL.limitGroups        = None
_wDL.machineList        = None
_wDL.blacklistMode      = None
_wDL.suspended          = None
_wDL.includeScene       = None
_wDL.taskCount          = None

frameSamplingOptions = [1,2,4,5,8,10,16,20,32,40,80,160]

#colors
class _c:
    pass

_c.default          = [0.333,0.333  ,0.333  ]
_c.highLight        = [0.7  ,0.7    ,0.4    ]
_c.activeFrame      = [0.3  ,0.3    ,0.35   ]
_c.objectsFrame     = [0.22 ,0.22   ,0.22   ]
_c.defaultField     = [0.28 ,0.28   ,0.28   ]

_c.meshOption       = [
                        [103/255.0  ,110/255.0  ,108/255.0  ],
                        [103/255.0  ,110/255.0  ,114/255.0  ],
                        [108/255.0  ,108/255.0  ,116/255.0  ]

                    ]

_c.pathFrame        = [
                        [75/255.0   ,90/255.0   ,85/255.0   ],
                        [70/255.0   ,90/255.0   ,100/255.0  ],
                        [80/255.0   ,70/255.0   ,90/255.0   ]

                    ]

_c.pathFrameBG      = [
                        [70/255.0,  75/255.0,   72/255.0    ],
                        [63/255.0   ,69/255.0   ,73/255.0   ],
                        [69/255.0   ,68/255.0   ,75/255.0   ]

                    ]

_c.pathEntry        = [
                        [42/255.0   ,41/255.0   ,48/255.0   ],
                        [36/255.0   ,43/255.0   ,46/255.0   ],
                        [37/255.0   ,43/255.0   ,42/255.0   ]
                    ]

_c.proxyFrame       = [
                        [0.306, 0.306,  0.306],
                        [75/255.0,  90/255.0,   85/255.0]
                    ]
_c.proxyFrameBG     = [
                        [0.267, 0.267,  0.267],
                        [70/255.0,  75/255.0,   72/255.0]
                    ]

_c.deadlineFrame    = [0.3  ,0.33   ,0.3    ]
_c.deadlineBG       = [0.2  ,0.22   ,0.2    ]

_c.error            = [0.6  ,0.2    ,0.2    ]

_gWidth = 600

_gDeadlineCommandCache = {}

def showXMeshSaver():
    if maya.cmds.pluginInfo('XMesh', query=True, loaded=True):
        UI = xmSaverUI()
        UI.UI()
    else:
        maya.cmds.confirmDialog(title='XMesh', m='XMesh is not loaded.\n\nPlease ensure that XMesh is loaded by using the Plug-in Manager.\n\nYou can access the Plug-in Manager by using the Window > Settings/Preferences > Plug-in Manager menu item.')

class xmSaverUI:
    def __init__(self):
        pass

###############################################################################
#
#   UI Controls
#
##############################################################################

    #Opens the UI
    #@classmethod
    def UI(self):
        #Check if window already exists
        if maya.cmds.window("XMeshSaver", exists=True):
            maya.cmds.deleteUI("XMeshSaver")

        #PlaybackOptions
        tmp = maya.cmds.playbackOptions()
        min = maya.cmds.playbackOptions(tmp, query=True, animationStartTime=True)
        max = maya.cmds.playbackOptions(tmp, query=True, animationEndTime=True)

        versionString = ''
        if maya.cmds.pluginInfo('XMesh', query=True, loaded=True):
            versionString = ' - v' + maya.cmds.pluginInfo('XMesh', query=True, version=True)

        # Definition of UI elements
        _w.windo = maya.cmds.window("XMeshSaver",
                                        mb=True,
                                        wh=(_gWidth+40,700),
                                        sizeable=False,
                                        rtf=False,
                                        title=(_s["title"] + versionString),
                                        mnb=False,
                                        mxb=False,
                                        )


    #todo: menuLayouts:
    #restore defaults, load user settings etc. about box

    # MENU
    #-------------------------------------------------------
        """
        maya.cmds.menu(l='Settings')
        maya.cmds.menuItem(l='Save Current Settings')
        maya.cmds.menuItem(l='Load Settings File')
        maya.cmds.menuItem(l='Reset Defaults')
        """
        helpmenu = maya.cmds.menu(l='Help', hm=True)
        onlineHelpItem = maya.cmds.menuItem(l='Online Help', c=self.onlineHelp)
        aboutItem = maya.cmds.menuItem(l='About XMesh', c=self.showAboutDialog)
    # SCROLL frame
    #-----------------------------------------------------

        maya.cmds.scrollLayout(w=_gWidth + 40, h=638)
        _w.scrollFrame = maya.cmds.columnLayout(adj=True)

    #FRAMELAYOUTS
    #-------------------------------------------------------
    #OBJECTS frame
        tmpLayout = maya.cmds.columnLayout(adj=True)
        maya.cmds.frameLayout(l=_s["ObjLabel"], cll=False)

        maya.cmds.columnLayout(width=_gWidth + 5, columnWidth=_gWidth + 5)

        _w.objList = maya.cmds.textScrollList(annotation=_s["ObjLabel"], numberOfRows=14, allowMultiSelection=True, width=_gWidth + 5)
        maya.cmds.popupMenu()
        _w.selectAll = maya.cmds.menuItem(label=_s["SelectAll"])
        _w.selectNone = maya.cmds.menuItem(label=_s["SelectNone"])
        _w.selectInverse = maya.cmds.menuItem(label=_s["SelectInverse"])


        maya.cmds.rowLayout(numberOfColumns=2, columnWidth2=(_gWidth/2,_gWidth/2), columnAttach=[(1,'both',0), (2,'both',0)])
        _w.add = maya.cmds.button(label=_s["AddObjects"])
        _w.rem = maya.cmds.button(label=_s["RemoveObjects"], en=False)
        maya.cmds.setParent('..')
        maya.cmds.separator(height=4)

        maya.cmds.setParent(_w.scrollFrame)

        self.appendObjectNames(_w.objList)
    #end OBJECTS frame



    #OUTPUT PATH frame
        _w.pathFrame = maya.cmds.frameLayout(l=_s['OutputFrameLabel'], cll=True)
        _w.pathFrameBG = maya.cmds.columnLayout()

        ###
        maya.cmds.rowLayout(numberOfColumns=2, columnAttach=[(1,'both',0), (2,'both',10)])
        _w.meshMode = maya.cmds.optionMenu(h=25) #,nbg=False) #, bgc=[0,0,.2],) w=_gWidth
        maya.cmds.menuItem(label=_s["MeshMode_World_Single"])
        maya.cmds.menuItem(label=_s["MeshMode_World_Separate"])
        maya.cmds.menuItem(label=_s["MeshMode_Object_Separate"])

        _w.subFolders = maya.cmds.checkBox(label=_s["MakeSubFolders"], enable=False)
        maya.cmds.setParent('..')

        maya.cmds.separator(height=6)

        maya.cmds.rowLayout(numberOfColumns=3, width=_gWidth, adjustableColumn3=2, columnAttach=[(1,'right',0), (2,'both',0), (3,'both',0)], columnWidth=[(1,142)])
        pathName = maya.cmds.text(label=_s["BasePathLabel"])
        _w.fPath = maya.cmds.textField(tx=maya.cmds.workspace(q=True, dir=True) + "cache/xmesh") #todo: default from outside this method
        browse = maya.cmds.symbolButton(image='navButtonBrowse.png')
        maya.cmds.setParent('..')

        maya.cmds.rowLayout(numberOfColumns=2, width=_gWidth, columnAttach=[(1,'right',0), (2,'both',0)], columnWidth=[(1,142)])
        maya.cmds.text(label=_s["SeqLabel"])
        _w.fProjectName =  maya.cmds.textField(tx="SEQ", width=175)
        maya.cmds.setParent('..')

        maya.cmds.rowLayout(numberOfColumns=2, width=_gWidth, columnAttach=[(1,'right',0), (2,'both',0)], columnWidth=[(1,142)])
        maya.cmds.text(label=_s["RevisionLabel"])
        _w.fRevision =  maya.cmds.textField(tx='v0001', width=175)
        maya.cmds.popupMenu()
        _w.lastRevision = maya.cmds.menuItem(label=_s["LastRevisionCmd"])
        _w.nextRevision = maya.cmds.menuItem(label=_s["NextRevisionCmd"])
        _w.clearRevision = maya.cmds.menuItem(label=_s["ClearRevisionCmd"])
        maya.cmds.setParent('..')

        maya.cmds.rowLayout(numberOfColumns=2, width=_gWidth, adjustableColumn2=2, columnAttach=[(1,'right',0), (2,'both',0)], columnWidth=[(1,142)])
        pathName = maya.cmds.text(label=_s['PreviewLabel'])
        _w.pathPreview = maya.cmds.textField(ed=False, tx='', fn='plainLabelFont') # "boldLabelFont", "smallBoldLabelFont", "tinyBoldLabelFont", "plainLabelFont", "smallPlainLabelFont", "obliqueLabelFont", "smallObliqueLabelFont", "fixedWidthFont" and "smallFixedWidthFont".
        maya.cmds.setParent('..')

        maya.cmds.separator(height=6)
        maya.cmds.setParent('..')
        maya.cmds.setParent(_w.scrollFrame)
    #end OUTPUT PATH frame
    #FRAMES frame
        _w.frameFrame = maya.cmds.frameLayout(l=_s['FrameRangeLabel'], cll=True)
        frameUIRows = maya.cmds.columnLayout(p=_w.frameFrame)   # Rows to hold subsequent rowLayouts for buttons and fields.

        # Row to contain label and buttons for setting frame range values.
        frameButtonRow = maya.cmds.rowColumnLayout(nc=5, p=frameUIRows, cw=[(1,142)], cal=[(1,'right')])
        maya.cmds.text(_s['SetFrameRangeLabel'])
        _w.frameScene = maya.cmds.button(label=_s['FrameRangeScene'])
        _w.framePlayback = maya.cmds.button(label=_s['FrameRangePlayback'])
        _w.frameRender = maya.cmds.button(label=_s['FrameRangeRender'])
        _w.frameCurrent = maya.cmds.button(label=_s['FramgeRangeCurrent'])

        # Row to contain  another layout that holds the frame range fields.
        frameRowColLayout = maya.cmds.rowColumnLayout(nc=2, rs=[(1,3),(2,3)], cal=[(1,'right'),(2,'left')], cw=[(1,142),(2,92)], p=frameUIRows) #,columnAttach=[(1,'both',0), (2,'both',0)])
        maya.cmds.text(_s['FrameStartLabel'])
        _w.startFrame = maya.cmds.intField(v=min)
        maya.cmds.text(_s['FrameEndLabel'])
        _w.endFrame = maya.cmds.intField(v=max)
        maya.cmds.text(_s['FrameSamples'])
        _w.subFrames = maya.cmds.optionMenu(w=50)

        for lab in frameSamplingOptions:
            maya.cmds.menuItem(label=str(lab))
        maya.cmds.setParent('..')
        maya.cmds.separator(height=4)
        maya.cmds.setParent(_w.scrollFrame)
    #end FRAMES frame
    #CHANNELS frame
        _w.channelFrame = maya.cmds.frameLayout(l=_s["ChannelFrameLabel"], cll=True, cl=True)
        maya.cmds.columnLayout()
        maya.cmds.text(l="Save these channels, if they exist:")
        maya.cmds.separator(height=4)
        _w.channelList = []
        for (chan,uiChan) in zip(xmChannels, xmUIChannels):
            value = chan in defaultXMChannels
            rowLayout = maya.cmds.rowLayout(numberOfColumns=2, columnWidth2=(_gWidth/3,_gWidth-_gWidth/3))
            _w.channelList.append(maya.cmds.checkBoxGrp(ncb=2, l1=uiChan, l2='Proxy', va2=[value,value], cc=saveSettings))
            if 'Color' in chan:
                _w.colorFromCurrentColorSet = maya.cmds.checkBoxGrp(ncb=1, l1='Save Current Color Set as color', v1=True, cc=saveSettings)
            elif 'TextureCoord' in chan:
                _w.map1FromCurrentUVSet = maya.cmds.checkBoxGrp(ncb=1, l1='Save Current UV Set as map1', v1=True, cc=saveSettings)
            else:
                maya.cmds.text(' ')
            maya.cmds.setParent('..')

        maya.cmds.separator(height=4)

        maya.cmds.setParent(_w.scrollFrame)
    #end CHANNELS frame


    #PROXY UI frame
        _w.proxyFrame = maya.cmds.frameLayout(l=_s["ProxyFrameLabel"], cll=True, cl=True)
        _w.proxyFrameBG = maya.cmds.columnLayout()

        maya.cmds.rowColumnLayout(numberOfRows=2,w=_gWidth, rowHeight=[(1, 22), (2, 22)], ral=[(1,"left"),(2,"left")])

        _w.useProxy =           maya.cmds.checkBox(label=_s["SaveProxy"])
        _w.useProxySampling =   maya.cmds.checkBox(label=_s["UseProxySampling"], enable=False)
        _w.proxyOptimization =  maya.cmds.optionMenu()
        for i in proxyOptimizationLabels:
            maya.cmds.menuItem(label=i, enable=True)

        _w.proxySubFrames = maya.cmds.optionMenu(enable=False)
        for lab in frameSamplingOptions:
            maya.cmds.menuItem(label=str(lab))

        _w.proxyPercent = maya.cmds.floatSliderGrp(label=_s['Percent'], enable=False, v=10, cw3=[80,60,60], f=True, min=0, max=100,pre=2, s=1.0, ss=1.0)
        maya.cmds.text(label="")

        maya.cmds.setParent(_w.scrollFrame)
    #end PROXY frame
    #CALLBACKS frame
        #todo:
    #end callbacks frame

    #PREFERENCES frame
        _w.preferencesFrame = maya.cmds.frameLayout(l=_s['PreferencesLabel'], cll=True, cl=True)
        maya.cmds.columnLayout(w=(_gWidth - 20))
        maya.cmds.separator(height=4)

        _w.visibleOnly = maya.cmds.checkBoxGrp(cal=[1,"left"], l='', l1='Save Visible Objects Only', ncb=1)

        maya.cmds.separator(height=4)

        _w.loggingLevel = maya.cmds.optionMenuGrp(l=_s['LoggingLevelLabel'], cl2=["right", "left"] , annotation="The minumum log level that is recorded.")
        for loggingLevel in loggingLevels:
            maya.cmds.menuItem(label=loggingLevel)

        maya.cmds.separator(height=4)

        maya.cmds.setParent(_w.scrollFrame)
    #end PREFERENCES frame

    #DEADLINE frame
        _wDL.frame = maya.cmds.frameLayout(l="Submit To Deadline", cll=True, cl=True, preExpandCommand=onDeadlineFramePreExpand)
        maya.cmds.columnLayout(w=(_gWidth - 20))
        maya.cmds.separator(height=4)
        
        if gUseDeadline:
            labelWidth = 30
            deadlineGroups = ["none"]
            deadlinePools = ["none"]
            savedJobPool = "none"
            savedGroup = "none"
            priority = 50
            machineLimit = 1
            limitCount = 0
            concurrentTasks = 0
            SlaveTimeout = 5000
            MinSlaveTimeout = 5000
            limitGroups = "abc"
            taskCount = 1
            attach2 = ["right", "left"]
            attach3 = ["right", "left", "left"]

            _wDL.jobName = maya.cmds.textFieldButtonGrp(cl3=attach3, l='Job Name:', buttonLabel="<", text=maya.cmds.file(q=True,sn=True,shn=True), annotation="The name of the job (press '<' button to use the scene file name)")
            maya.cmds.textFieldButtonGrp(_wDL.jobName, e=True, bc=partial(setJobName, _wDL.jobName))

            _wDL.comment = maya.cmds.textFieldGrp(l="Comment:", cl2=attach2, cw1=labelWidth, text="XMesh for Maya", annotation="A brief comment about the job")
            _wDL.department = maya.cmds.textFieldGrp(l="Department:", cl2=attach2, cw1=labelWidth, text="", annotation="The department the job (or the job's user) belongs to")

            _wDL.pool = maya.cmds.optionMenuGrp(l="Pool:", cl2=attach2 , annotation="The pool the job belongs to")

            _wDL.group = maya.cmds.optionMenuGrp(l="Group:", cl2=attach2, annotation="The group the job belongs to")

            maya.cmds.separator(height=5)

            _wDL.priority = maya.cmds.intSliderGrp(l="Priority:", cl3=attach3, cw1=labelWidth, field=True, minValue=0, maxValue=100, v=priority, annotation="The job's priority (0 is the lowest, 100 is the highest)")

            maya.cmds.separator(height=5)

            _wDL.machineLimit = maya.cmds.intSliderGrp(l="Machine Limit:", cl3=attach3, cw1=labelWidth, field=True, minValue=0, maxValue=100, v=machineLimit, annotation="The maximum number of machines that can run your job at one time.  Specify 0 for no limit.")
            _wDL.taskCount = maya.cmds.intSliderGrp(l="Tasks:", cl3=attach3, cw1=labelWidth, field=True, minValue=1, maxValue=100, v=taskCount, annotation="Job will be split into this number of tasks.  Multiple tasks allows saving on multiple machines in parallel.")

            _wDL.limitGroups = maya.cmds.textFieldButtonGrp(cl3=attach3, cw1=labelWidth,  l="Limits:", buttonLabel="...", text='', annotation="The limit groups that this job requires")
            maya.cmds.textFieldButtonGrp(_wDL.limitGroups, e=True, bc=partial(setDeadlineList, _wDL.limitGroups, "SelectLimitGroups"))

            _wDL.machineList = maya.cmds.textFieldButtonGrp(cl3=attach3, cw1=labelWidth,  l="Machine List:", buttonLabel="...", text='', annotation="The allowlisted or denylisted machines")
            maya.cmds.textFieldButtonGrp(_wDL.machineList, e=True, bc=partial(setDeadlineList, _wDL.machineList, "SelectMachineList"))

            _wDL.blacklistMode = maya.cmds.radioButtonGrp(l='', la2=['Denylist','Allowlist'], nrb=2, sl=2)
            maya.cmds.separator(height=10)

            _wDL.includeScene = maya.cmds.checkBoxGrp(cal=[1,"left"], l='', label1='Submit Maya Scene File', ncb=1, annotation="Maya File will be copied with the job.")

            _wDL.suspended = maya.cmds.checkBoxGrp(cal=[1,"left"], l='', label1='Submit Suspended', ncb=1, annotation="Job will be suspended in the queue")

            maya.cmds.separator(height=10)

            maya.cmds.rowLayout(numberOfColumns=2, columnWidth=[1,140])
            maya.cmds.text(label="")
            deadlineBtn = maya.cmds.button(label="SUBMIT TO DEADLINE", width=240, height=40)
            maya.cmds.setParent('..')
            maya.cmds.separator(height=10)

            maya.cmds.setParent(_w.scrollFrame)

            maya.cmds.button(deadlineBtn, edit=True, command=self.submitToDeadline)

            #Save deadline settings when a field has been edited
            maya.cmds.textFieldButtonGrp(_wDL.jobName, e=True, cc=saveSettings)
            maya.cmds.textFieldButtonGrp(_wDL.limitGroups, e=True, cc=saveSettings)
            maya.cmds.textFieldButtonGrp(_wDL.machineList, e=True, cc=saveSettings)

            maya.cmds.textFieldGrp(_wDL.comment, e=True, cc=saveSettings)
            maya.cmds.textFieldGrp(_wDL.department, e=True, cc=saveSettings)

            maya.cmds.optionMenuGrp(_wDL.pool, edit=True, cc=saveSettings)
            maya.cmds.optionMenuGrp(_wDL.group, edit=True, cc=saveSettings)

            maya.cmds.intSliderGrp(_wDL.priority, edit=True, cc=saveSettings)

            maya.cmds.radioButtonGrp(_wDL.blacklistMode, edit=True, cc=saveSettings)

            maya.cmds.checkBoxGrp(_wDL.includeScene, edit=True, cc=saveSettings)
            maya.cmds.checkBoxGrp(_wDL.suspended, edit=True, cc=saveSettings)

            maya.cmds.intSliderGrp(_wDL.machineLimit, edit=True, cc=self.machineLimitChangedCB)
            maya.cmds.intSliderGrp(_wDL.taskCount, edit=True, cc=self.taskCountChangedCB)
        elif gDLInvokeError:
            maya.cmds.text(DL._s['detectionErrorMsg'], align='left')
        else:
            url = DL._s['downloadAddress']

            maya.cmds.text(DL._s['noDeadlineMsg'], align='left')

            maya.cmds.rowLayout(numberOfColumns=2)
            maya.cmds.text(label=url, hyperlink=True)
            maya.cmds.button(label='Go', command='import maya; maya.cmds.launch(web="' + url + '")')
            maya.cmds.setParent('..')

            maya.cmds.separator(height=20)
            
    #end DEADLINE frame

        maya.cmds.setParent(_w.windo)
    #SAVE BUTTONS
        maya.cmds.rowLayout(numberOfColumns=2, width=_gWidth, adjustableColumn2=1, columnAttach=[(1,'both',0), (2,'both',0)])
        save = maya.cmds.button(label=_s['saveBtn'], h=40)
        close = maya.cmds.button(label=_s['closeBtn'], h=40, w=50)
        maya.cmds.setParent('..')
    #end SAVE BUTTONS

        maya.cmds.setParent('..')

        # Definition of UI element interaction
        maya.cmds.symbolButton(     browse,         edit=True, command=partial(self.browseCB,_w.fPath))
        maya.cmds.button(           save,               edit=True, command=self.doSave)

        maya.cmds.button(           close,          edit=True, command=self.closeWindow)
        maya.cmds.button(           _w.add,         edit=True, command=partial(self.appendObjectNames, _w.objList))
        maya.cmds.button(           _w.rem,         edit=True, command=partial(self.removeItem, _w.objList))
        maya.cmds.button(           _w.frameScene,      edit=True, command=self.setFramesToSceneRange)
        maya.cmds.button(           _w.framePlayback,   edit=True, command=self.setFramesToPlaybackRange)
        maya.cmds.button(           _w.frameRender,     edit=True, command=self.setFramesToRenderRange)
        maya.cmds.button(           _w.frameCurrent,        edit=True, command=self.setFramesToCurrentFrame)

        maya.cmds.textScrollList(       _w.objList,         edit=True, sc=self.selChangedCB)

        maya.cmds.menuItem(         _w.selectAll,       edit=True, command=partial(self.selectAll, _w.objList))
        maya.cmds.menuItem(         _w.selectInverse,   edit=True, command=partial(self.selectInverse, _w.objList))
        maya.cmds.menuItem(         _w.selectNone,      edit=True, command=partial(self.selectNone, _w.objList))
        maya.cmds.menuItem(         _w.lastRevision,        edit=True, command=self.setLastRevisionCommand)
        maya.cmds.menuItem(         _w.nextRevision,        edit=True, command=self.setNextRevisionCommand)
        maya.cmds.menuItem(         _w.clearRevision,       edit=True, command=self.clearRevisionCommand)

        maya.cmds.textField(            _w.fPath,           edit=True, cc=self.pathChangedCB)
        maya.cmds.textField(            _w.fProjectName,        edit=True, cc=self.pathChangedCB)
        maya.cmds.textField(            _w.fRevision,       edit=True, cc=self.pathChangedCB)

        maya.cmds.checkBox(         _w.useProxy,        edit=True, onc=partial(self.proxyCB, True), ofc=partial(self.proxyCB, False))
        maya.cmds.checkBox(         _w.useProxySampling,    edit=True, cc=self.useProxySamplingChangedCB)
        maya.cmds.optionMenu(       _w.proxyOptimization,   edit=True, cc=self.proxyOptimizationChangedCB)
        maya.cmds.checkBox(         _w.subFolders,      edit=True, cc=self.pathChangedCB)
        maya.cmds.checkBoxGrp(      _w.visibleOnly,     edit=True, cc=saveSettings)

        maya.cmds.optionMenu(       _w.meshMode,        edit=True, cc=self.meshModeCB) #partial(self.meshModeCB, _w.meshMode))
        maya.cmds.optionMenuGrp(        _w.loggingLevel,        edit=True, cc=self.loggingLevelCB)
        maya.cmds.intField(         _w.startFrame,      edit=True, dc=partial(self.adjustEndFrame, _w.startFrame, _w.endFrame), cc=saveSettings)
        maya.cmds.intField(         _w.endFrame,        edit=True, dc=partial(self.adjustEndFrame, _w.startFrame, _w.endFrame), cc=saveSettings)

        maya.cmds.frameLayout(      _w.pathFrame,       edit=True, cc=partial(self.pathFrameCollapseCB, False), ec=partial(self.pathFrameCollapseCB, True))
        maya.cmds.frameLayout(      _w.frameFrame,      edit=True, cc=partial(self.framesFrameCollapseCB,False),    ec=partial(self.framesFrameCollapseCB, True))
        maya.cmds.frameLayout(      _w.channelFrame,    edit=True, cc=partial(self.channelsFrameCollapseCB,False),  ec=partial(self.channelsFrameCollapseCB, True))
        maya.cmds.frameLayout(      _w.proxyFrame,      edit=True, cc=partial(self.proxyFrameCollapseCB,False), ec=partial(self.proxyFrameCollapseCB, True))
        maya.cmds.frameLayout(      _w.preferencesFrame,    edit=True, cc=partial(self.advancedFrameCollapseCB,False),  ec=partial(self.advancedFrameCollapseCB, True))

        #save settings only
        maya.cmds.optionMenu(       _w.subFrames,       edit=True, cc=saveSettings)
        maya.cmds.floatSliderGrp(       _w.proxyPercent,        edit=True, cc=saveSettings)
        maya.cmds.optionMenu(       _w.proxySubFrames,  edit=True, cc=saveSettings)

        maya.cmds.showWindow()

        maya.cmds.optionMenuGrp(_w.loggingLevel, e=True, sl=(1+maya.cmds.xmeshLogging(q=True, loggingLevel=True)))
        loadSettings(self)

        revision = maya.cmds.textField(_w.fRevision, q=True, tx=True)
        if self.isRevisionString(revision):
            self.setNextRevisionCommand()

        self.updateProxyControls(maya.cmds.checkBox(_w.useProxy, q=True, v=True))

        self.pathFrameCollapseCB(not (maya.cmds.frameLayout(_w.pathFrame, q=True, cl=True)))
        self.framesFrameCollapseCB(not (maya.cmds.frameLayout(_w.frameFrame, q=True, cl=True)))
        self.channelsFrameCollapseCB(not (maya.cmds.frameLayout(_w.channelFrame, q=True, cl=True)))
        self.proxyFrameCollapseCB(not (maya.cmds.frameLayout(_w.proxyFrame, q=True, cl=True)))
        self.advancedFrameCollapseCB(not (maya.cmds.frameLayout(_w.preferencesFrame, q=True, cl=True)))

        self.meshModeCB(maya.cmds.optionMenu(_w.meshMode,q=True,v=True)) #todo factor out to second init of control vis and state.
        self.updateDeadlineUI()
        return


###############################################################################
#
#   Behaviors
#
##############################################################################

    def getRevisionRegex(self):
        return re.compile('v([0-9]+)$')

    def isRevisionString(self, s):
        regex = self.getRevisionRegex()
        return regex.match(s) is not None

    def findLastRevision(self, *args):
        revisionText = None
        oldRevisionText = maya.cmds.textField(_w.fRevision, q=True, tx=True)

        revisionDir = os.path.dirname(self.getFullPath())
        if len(oldRevisionText) > 0:
            revisionDir = os.path.dirname(revisionDir)

        if os.path.isdir(revisionDir):
            regex = self.getRevisionRegex()
            revisions = [m.group(1) for entry in os.listdir(revisionDir)
                            for m in (regex.match(entry),) if m]
            revisions.sort(key=lambda x: int(x), reverse=True)
            for rev in revisions:
                subDir = 'v' + rev
                if os.path.isdir(os.path.join(revisionDir, subDir)):
                    revisionText = subDir
                    break

        return revisionText

    def setLastRevisionCommand(self, *args):
        revisionText = self.findLastRevision()
        if revisionText is None:
            revisionText = "v0001"

        maya.cmds.textField(_w.fRevision, edit=True, tx=revisionText)
        self.pathChangedCB(*args)

    def setNextRevisionCommand(self, *args):
        revisionText = self.findLastRevision()

        if revisionText is None:
            revisionText = "v0001"
        else:
            revisionNum = revisionText[1:len(revisionText)]
            revisionText = "v" + str(int(revisionNum) + 1).zfill(len(revisionNum))

        maya.cmds.textField(_w.fRevision, edit=True, tx=revisionText)
        self.pathChangedCB(*args)

    def clearRevisionCommand(self, *args):
        maya.cmds.textField(_w.fRevision, edit=True, tx="")
        self.pathChangedCB(*args)

    def doSave(self, *args):
        fullDir = os.path.dirname(self.getFullPath())
        baseDir = maya.cmds.textField(_w.fPath, q=True, tx=True)
        doNothing = False

        numObjs = maya.cmds.textScrollList(_w.objList, q=True, ni=True);
        if numObjs <= 0:
            maya.cmds.confirmDialog(title='Error', message='No objects have been selected.\n\nPlease select an object to save.', button=['OK'], defaultButton='OK')
            doNothing = True
        elif not os.path.isdir(fullDir):
            if not os.path.isdir(baseDir):
                answer = maya.cmds.confirmDialog(title='Create Directory?',
                                                 message='The specified directory does not exist. Would you like to create it?',
                                                 button=['Create', 'Cancel'], defaultButton='Create', cancelButton='Cancel',
                                                 dismissString='Cancel', messageAlign='left')
                if answer == 'Create':
                    os.makedirs(fullDir)
                else:
                    doNothing = True
            else:
                os.makedirs(fullDir)

        if not doNothing:
            ss = self.prepSettings()
            proc = saveXMeshSequence.XMeshSaverProc(ss)


    def setFramesToSceneRange(self, *args):
        sceneStartFrame = maya.cmds.playbackOptions(query=True, animationStartTime=True)
        sceneEndFrame = maya.cmds.playbackOptions(query=True, animationEndTime=True)
        maya.cmds.intField(_w.startFrame, edit=True, value=sceneStartFrame)
        maya.cmds.intField(_w.endFrame, edit=True, value=sceneEndFrame)
        saveSettings()


    def setFramesToPlaybackRange(self, *args):
        playbackStartFrame = maya.cmds.playbackOptions(query=True, minTime=True)
        playbackEndFrame = maya.cmds.playbackOptions(query=True, maxTime=True)
        maya.cmds.intField(_w.startFrame, edit=True, value=playbackStartFrame)
        maya.cmds.intField(_w.endFrame, edit=True, value=playbackEndFrame)
        saveSettings()


    def setFramesToRenderRange(self, *args):
        renderStartFrame = maya.cmds.getAttr('defaultRenderGlobals.startFrame')
        renderEndFrame = maya.cmds.getAttr('defaultRenderGlobals.endFrame')
        maya.cmds.intField(_w.startFrame, edit=True, value=renderStartFrame)
        maya.cmds.intField(_w.endFrame, edit=True, value=renderEndFrame)
        saveSettings()

    def setFramesToCurrentFrame(self, *arg):
        renderStartFrame = maya.cmds.currentTime(query=True)
        renderEndFrame = maya.cmds.currentTime(query=True)
        maya.cmds.intField(_w.startFrame, edit=True, value=renderStartFrame)
        maya.cmds.intField(_w.endFrame, edit=True, value=renderEndFrame)
        saveSettings()

    def submitToDeadline(self,*args):
        ss = self.prepSettings(deadline=True)

        dls = self.prepDeadlineSettings(*args)

        DL.saveOverDeadline(ss, dls)


    def prepSettings(self, deadline=False):
        #copy UI entries into settings

        ss = saveXMeshSequence.xmSettings()
        ss.fullPath     = self.getFullPath()
        ss.objectList   = maya.cmds.textScrollList( _w.objList,     q=True, allItems=True)

        mshmode         = maya.cmds.optionMenu(     _w.meshMode,    q=True, value=True)
        if mshmode == _s['MeshMode_Object_Separate']:
            ss.worldSpace = False
            ss.combineObjects = False
        else:
            ss.worldSpace = True
            if mshmode == _s['MeshMode_World_Separate']:
                ss.combineObjects = False
            else:
                ss.combineObjects = True

        #   build full path
        #   build channel mask string eg. "Velocity,Normals,Texcoord"
        (ss.channelMask, ss.proxyChannelMask) = getChannelLists()
        ss.channelProperties = getChannelProperties()

        ss.subFolders   = maya.cmds.checkBox(       _w.subFolders,  q=True, value=True)
        ss.startFrame   = maya.cmds.intField(       _w.startFrame,  q=True, value=True)
        ss.endFrame     = maya.cmds.intField(       _w.endFrame,    q=True, value=True)
        ss.frameStep    = 1.0 / int(maya.cmds.optionMenu(   _w.subFrames,   q=True, value=True))

        #proxy
        ss.saveProxy        = int(maya.cmds.checkBox(       _w.useProxy,    q=True, value=True))
        useProxySampling = maya.cmds.checkBox(_w.useProxySampling, q=True, value=True)
        if useProxySampling:
            ss.proxyFrameStep = 1.0 / int(maya.cmds.optionMenu( _w.proxySubFrames,  q=True, value=True))
        else:
            ss.proxyFrameStep = ss.frameStep

        ss.proxyPercent     = maya.cmds.floatSliderGrp( _w.proxyPercent,    q=True, value=True)
        ss.visibleOnly = maya.cmds.checkBoxGrp(_w.visibleOnly,  q=True, value1=True)

        ss.proxyOptimization = proxyOptimizationLabelToMode[maya.cmds.optionMenu(_w.proxyOptimization, q=True, value=True)]

        if deadline:
            ss.useDeadlineOutputDirectory = True

            taskCount = maya.cmds.intSliderGrp(_wDL.taskCount, q=True, value=True)
            ss.deadlineTaskCount = taskCount

        return ss

    def prepDeadlineSettings(self, *args):
        dls = DL.deadlineSettings()

        dls.d['Name']           = maya.cmds.textFieldButtonGrp(_wDL.jobName,    q=True,text=True)
        dls.d['Comment']        = maya.cmds.textFieldGrp(       _wDL.comment,   q=True,text=True)
        dls.d['Department']     = maya.cmds.textFieldGrp(       _wDL.department,q=True,text=True)
        dls.d['Pool']           = maya.cmds.optionMenuGrp(      _wDL.pool,      q=True,value=True)
        dls.d['Group']          = maya.cmds.optionMenuGrp(      _wDL.group,     q=True,value=True)
        dls.d['OutputDirectory0']   = os.path.dirname(self.getFullPath())
        dls.d['MachineLimit']   = maya.cmds.intSliderGrp(       _wDL.machineLimit, q=True, value=True)
        taskCount = maya.cmds.intSliderGrp(_wDL.taskCount, q=True, value=True)
        frames = '0'
        if taskCount > 1:
            frames = '0-' + str(taskCount - 1)
        dls.d['Frames'] = frames
        limitGroups = sanitizeDeadlineList(maya.cmds.textFieldButtonGrp(_wDL.limitGroups,q=True,text=True))
        if limitGroups != '':
            dls.d['LimitGroups']    = sanitizeDeadlineList(maya.cmds.textFieldButtonGrp(_wDL.limitGroups,q=True,text=True))

        if maya.cmds.radioButtonGrp(_wDL.blacklistMode, q=True, sl=True)==1:
            listMode = 'Blacklist'
        else:
            listMode = 'Whitelist'

        machineList = sanitizeDeadlineList(maya.cmds.textFieldButtonGrp(_wDL.machineList,q=True,text=True))
        if machineList != '':
            dls.d[listMode] = machineList

        dls.d['Priority']   = maya.cmds.intSliderGrp(       _wDL.priority,  q=True,value=True)

        if maya.cmds.checkBoxGrp(_wDL.suspended, q=True, value1=True):
            dls.d['InitialStatus'] = 'Suspended'
        if maya.cmds.checkBoxGrp(_wDL.includeScene, q=True, value1=True):
            dls.p['IncludeSceneFile'] = True
        return dls

    def updateDeadlineUI(self, *args):
        pass

    def closeWindow(self, *args):
        saveSettings()
        maya.cmds.deleteUI(_w.windo, window=True)

    def removeItem(self, ctrl, *args):
        sItems = maya.cmds.textScrollList(ctrl, query=True, selectItem=True)
        if sItems:
            maya.cmds.textScrollList(ctrl, edit=True, removeItem=sItems)
            self.selChangedCB(*args)

    def selectAll(self, ctrl, *args):
        numberOfItems = maya.cmds.textScrollList(ctrl, query=True, numberOfItems=True)
        for i in range(1, numberOfItems+1):
            maya.cmds.textScrollList(ctrl, edit=True, selectIndexedItem=i)
        self.selChangedCB(*args)

    def selectInverse(self, ctrl, *args):
        numberOfItems = maya.cmds.textScrollList(ctrl, query=True, numberOfItems=True)
        selectedItems = maya.cmds.textScrollList(ctrl, query=True, selectIndexedItem=True)
        if selectedItems is None:
            selectedItems = []
        selected = [False] * numberOfItems
        for i in selectedItems:
            selected[i-1] = True
        for i in range(1, numberOfItems+1):
            if selected[i-1]:
                maya.cmds.textScrollList(ctrl, edit=True, deselectIndexedItem=i)
            else:
                maya.cmds.textScrollList(ctrl, edit=True, selectIndexedItem=i)
        self.selChangedCB(*args)

    def selectNone(self, ctrl, *args):
        maya.cmds.textScrollList(ctrl, edit=True, deselectAll=True)
        self.selChangedCB(*args)

    def toggleCtrls(self, items, state):
        for item in items:
            maya.cmds.control(item, edit=True, enable=state)

    def toggleCtrl(self, item, state, *args):
        maya.cmds.control(item, edit=True, enable=state)


    #UIcallbacks
    def selChangedCB(self,  *args):
        nSelected = maya.cmds.textScrollList(_w.objList, q=True, nsi=True)
        self.toggleCtrl(_w.rem, nSelected != 0, *args)

    def updateProxyControls(self, val=None):
        if val is None:
            val = maya.cmds.checkBox(_w.useProxy, q=True, v=True)

        colorIndex = 1 if val else 0
        color = _c.proxyFrame[colorIndex]
        colorBG = _c.proxyFrameBG[colorIndex]

        proxyControlList = [_w.useProxySampling, _w.proxyOptimization]
        reduceControlList = [_w.proxyPercent]
        subFrameControlList = [_w.proxySubFrames]

        maya.cmds.frameLayout(_w.proxyFrame, e=True, bgc=color, ebg=(not val)) #, nbg=(not val))
        maya.cmds.columnLayout(_w.proxyFrameBG, e=True, bgc=colorBG)

        enableProxyControls = val
        enableReduceControls = (proxyOptimizationLabelToMode[maya.cmds.optionMenu(_w.proxyOptimization, q=True, v=True)] == 'polyReduce')
        enableSubFrameControls = maya.cmds.checkBox(_w.useProxySampling,    q=True, v=True)

        self.toggleCtrls(proxyControlList, enableProxyControls)
        self.toggleCtrls(reduceControlList, enableProxyControls and enableReduceControls)
        self.toggleCtrls(subFrameControlList, enableProxyControls and enableSubFrameControls)

    def proxyCB(self, val, *args):
        self.updateProxyControls(val)
        self.channelsFrameCollapseCB(not (maya.cmds.frameLayout(_w.channelFrame, q=True, cl=True)))
        saveSettings()

    def browseCB(self, *args):
        startingDirectory = maya.cmds.textField(_w.fPath, q=True, tx=True)
        filePath = maya.cmds.fileDialog2(dialogStyle=2, fileMode=3, okCaption='OK', startingDirectory=startingDirectory)
        if filePath is not None:
            self.setBasePath(filePath[0]) #first file
            self.pathChangedCB(*args)


    def pathChangedCB(self, *args):
        maya.cmds.textField(_w.pathPreview, e=True, tx=self.pathPreview(*args))
        saveSettings(*args)

    def meshModeCB(self, val, *args):
        #todo: modify UI for different modes

        if val == _s["MeshMode_World_Single"]:
            mode = 0
            sf = False
        else:
            sf = True
            if val == _s["MeshMode_Object_Separate"]:
                mode = 2
            else:
                mode = 1
        maya.cmds.checkBox(_w.subFolders,   e=True, enable=sf)
        maya.cmds.optionMenu(_w.meshMode,   e=True, bgc=_c.meshOption[mode])
        maya.cmds.control(_w.pathFrame,     e=True, bgc=_c.pathFrame[mode])
        maya.cmds.control(_w.pathFrameBG,   e=True, bgc=_c.pathFrameBG[mode])
        maya.cmds.control(_w.fPath,         e=True, bgc=_c.pathEntry[mode])
        maya.cmds.control(_w.fRevision,     e=True, bgc=_c.pathEntry[mode])
        maya.cmds.control(_w.fProjectName,  e=True, bgc=_c.pathEntry[mode])
        maya.cmds.control(_w.pathPreview,   e=True, bgc=_c.pathFrameBG[mode])
        self.pathChangedCB(*args)

    def loggingLevelCB(self, val):
        maya.cmds.xmeshLogging(loggingLevel=loggingLevels.index(val))
        saveSettings()

    def pathFrameCollapseCB(self, val, *args):
        if val == True:
            str = _s['OutputFrameLabel']
        else:
            str = "%s       %s" % (_s['OutputFrameLabel'], self.pathPreview(args))
        maya.cmds.frameLayout(_w.pathFrame, e=True, l=str)
        saveSettings()

    def framesFrameCollapseCB(self, val, *args):
        if val == True:
            str = _s['FrameRangeLabel']
        else:
            s = maya.cmds.intField(_w.startFrame,   q=True, v=True)
            e = maya.cmds.intField(_w.endFrame,     q=True, v=True)
            samplingvalue = maya.cmds.optionMenu(_w.subFrames,  q=True, v=True)
            str = "%s          [ %d..%d" % (_s['FrameRangeLabel'], s, e)
            if samplingvalue != "1":
                splural = ""
                if samplingvalue != "1":
                    splural = "s"
                str += ", %s Sample%s Per Frame" % (samplingvalue, splural)
            str += " ]"
        maya.cmds.frameLayout(_w.frameFrame, e=True, l=str)
        saveSettings()

    def getChannelSummaryString(self, channelList):
        s = ""
        for (chan,uiChan) in zip(xmChannels, xmUIChannels):
            if chan.startswith('Mapping2'):
                chan = 'Mapping2'
            if chan.startswith('EdgeSharpness'):
                chan = 'EdgeSharpness'
            value = chan in channelList
            if uiChan.startswith('map'):
                name = uiChan[:4]
            else:
                name = uiChan[:3]
            if value:
                s += name +", "
        s = s[:-2]
        return s

    def channelsFrameCollapseCB(self, val, *args):
        if val == True:
            str = _s['ChannelFrameLabel']
        else:
            str = _s['ChannelFrameLabel'] +"       [ "

            (channelList,proxyChannelList) = getChannelLists()
            mainSummary = self.getChannelSummaryString(channelList)
            proxySummary = self.getChannelSummaryString(proxyChannelList)

            useProxy = maya.cmds.checkBox(_w.useProxy, q=True, v=True)
            if useProxy and mainSummary != proxySummary:
                str += "Main: %s | Proxy: %s " % (mainSummary, proxySummary)
            else:
                str += mainSummary

            str = str + " ]"
        maya.cmds.frameLayout(_w.channelFrame, e=True, l=str)
        saveSettings()

    def proxyFrameCollapseCB(self, val, *args):
        if val == True:
            str = _s['ProxyFrameLabel']
        else:
            use = maya.cmds.checkBox(_w.useProxy,   q=True, v=True)
            if use:
                useSubFrames = maya.cmds.checkBox(_w.useProxySampling,  q=True, v=True)
                proxyOptimization = maya.cmds.optionMenu(_w.proxyOptimization,  q=True, v=True)
                subFramesValue = maya.cmds.optionMenu(_w.proxySubFrames,    q=True, v=True)
                percent = maya.cmds.floatSliderGrp(_w.proxyPercent, q=True, v=True)

                if proxyOptimization == 'Reduce':
                    reduceString = ", Reduce To %d%%" % (percent)
                else:
                    reduceString = ", Original"

                if useSubFrames == True:
                    splural = ""
                    if subFramesValue != "1":
                        splural = "s"

                    subFramesString = ", %s Sample%s Per Frame"  % (subFramesValue, splural)
                else:
                    subFramesString = ""

                str = "%s            [ ON%s%s ]" % (_s['ProxyFrameLabel'], reduceString, subFramesString )
            else:
                str = _s['ProxyFrameLabel']
        maya.cmds.frameLayout(_w.proxyFrame, e=True, l=str)
        saveSettings()

    def useProxySamplingChangedCB(self, val, *args):
        self.updateProxyControls()
        saveSettings()

    def proxyOptimizationChangedCB(self, val, *args):
        self.updateProxyControls()
        saveSettings()

    def advancedFrameCollapseCB (self, val, *args):
        if val == True:
            str = _s['PreferencesLabel']
        else:
            visibleonly = maya.cmds.checkBoxGrp(_w.visibleOnly, q=True, value1=True)
            debugLevel = maya.cmds.optionMenuGrp(_w.loggingLevel,   q=True, v=True)
            if visibleonly == True:
                visibleString = "Visible Only"
            else:
                visibleString = ""
            summary = "%s" % visibleString
            if len(summary) > 0:
                summary = '[ ' + summary + ' ]'
            str = "%s    %s" % (_s['PreferencesLabel'], summary)
        maya.cmds.frameLayout(_w.preferencesFrame, e=True, l=str)
        saveSettings()

    def machineLimitChangedCB(self, val, *args):
        taskCount = maya.cmds.intSliderGrp(_wDL.taskCount, q=True, v=True)
        if val > taskCount:
            maya.cmds.intSliderGrp(_wDL.taskCount, e=True, v=val)
        saveSettings()

    def taskCountChangedCB(self, val, *args):
        machineLimit = maya.cmds.intSliderGrp(_wDL.machineLimit, q=True, v=True)
        if val < machineLimit:
            maya.cmds.intSliderGrp(_wDL.machineLimit, e=True, v=val)
        saveSettings()

    # adjusts the endFrame field so that it never holds a value lower than the startFrame
    def adjustEndFrame(self, startFrame, endFrame, *args):
        startVal = maya.cmds.intField(startFrame, query=True, value=True)
        if maya.cmds.intField(endFrame, query=True, value=True) < startVal:
            maya.cmds.intField(endFrame, edit=True, value=startVal)
        self.pathChangedCB(*args)
        if maya.cmds.frameLayout(_w.pathFrame, query=True, collapse=True) == True:
            self.pathFrameCollapseCB(False,*args)

    # defines the actions of the browse button
    def getFullPath(self, *args):
        fpath = ""
        path                = maya.cmds.textField(_w.fPath,         q=True, tx=True)
        project             = maya.cmds.textField(_w.fProjectName,  q=True, tx=True)
        revision            = maya.cmds.textField(_w.fRevision,     q=True, tx=True)
        extension           = '.xmesh'
        mshmod              = maya.cmds.optionMenu(_w.meshMode, q=True, value=True)
        separateFolders     = maya.cmds.checkBox(_w.subFolders, q=True, value=True)

        if revision != "":
            if not revision.endswith('/'): revision+='/'
        if not path.endswith('/'): path+='/'

        fpath = path + project + '/' + revision + project + '_' + extension
        return fpath

    def setBasePath(self, fpath):
        maya.cmds.textField(_w.fPath,           e=True, tx=fpath)
        saveSettings()

    def pathPreview(self, *args):
        fpath = ""
        path                = maya.cmds.textField(_w.fPath,         q=True, tx=True)
        project             = maya.cmds.textField(_w.fProjectName,  q=True, tx=True)
        revision            = maya.cmds.textField(_w.fRevision,     q=True, tx=True)
        extension           = '.xmesh'
        mshmod              = maya.cmds.optionMenu(_w.meshMode, q=True, value=True)
        separateFolders     = maya.cmds.checkBox(_w.subFolders, q=True, value=True)

        framePart           = "_%4.4d" % maya.cmds.intField(_w.startFrame, q=True, value=True)

        path = "$(Base Path)/"

        if revision != "":
            if not revision.endswith('/'): revision+='/'
        if not path.endswith('/'): path+='/'

        if mshmod == _s["MeshMode_World_Single"]:
            fpath = path + project + '/' + revision + project + framePart + extension
        else:
            if separateFolders == False:
                fpath = path + project + '/' + revision + '$(obj)' + framePart + extension
            else:
                fpath = path + project + '/' + revision + '$(obj)/$(obj)' + framePart + extension

        att = fpath
        if len(fpath) > 92:
            t1 = len(fpath)-92
            s2 = fpath.find('/',3)
            s3 = fpath.find(project, s2)-1
            s4 = fpath.rfind('/', s2+1, s3)
            t2 = t1 - s4 + s2 + 4
            if t2 > 0:
                s4+=t2

            fpath = fpath[:s2] + "/..." + fpath[s4:]

        return fpath

    # appends newly selected items to the list
    def appendObjectNames(self, names, *args):
        if names is None:
            return
        tmp = []
        tmp2 = []
        checkList = []
        selectChildren = True

        #Get Select Objects
        if selectChildren:
            oldSelection = maya.cmds.ls(sl=True)
            maya.cmds.select(hi=True)
            tflt = maya.cmds.itemFilter (byType='transform')
            sflt = maya.cmds.itemFilter (byType='mesh')
            flt = maya.cmds.itemFilter (un=(tflt,sflt))
            prospects = maya.cmds.lsThroughFilter(tflt, sl=True)
            if oldSelection:
                maya.cmds.select(oldSelection)
        else:
            prospects = maya.cmds.ls(long=False, selection=True, type='transform')

        prospects = [i for i in prospects if canSave(i)]

        #strip left '|' (not sure if this makes sense)
        for i in prospects:
            tmp.append(i.lstrip('|'))

        #Get Objects in List Already
        checkList = maya.cmds.textScrollList(names, query=True, allItems=True)
        if checkList is None: checkList = []

        #remove duplicates
        checkList = list(set(checkList))
        for i in tmp:
            if i not in checkList:
                tmp2.append(i)

        #Set List
        maya.cmds.textScrollList(names, edit=True, append=tmp2)

    def onlineHelp(self, *args):
        maya.mel.eval('source showXMeshHelp')
        maya.mel.eval('showXMeshHelp')

    def showAboutDialog(self, *args):
        XMeshNoticesDialog.open_xmeshmy_notices_dialog()

def saveSettings(*args):
    #todo Save settings for object list

    if _w.fPath:            saveStringFileInfo("xmeshsaver_txt_fpath",              maya.cmds.textField(_w.fPath,               q=True, tx=True))
    if _w.fRevision:        saveStringFileInfo("xmeshsaver_txt_fRevision",          maya.cmds.textField(_w.fRevision,           q=True, tx=True))
    if _w.fProjectName:     saveStringFileInfo("xmeshsaver_txt_fProjectName",       maya.cmds.textField(_w.fProjectName,        q=True, tx=True))
    if _w.fExtension:       saveStringFileInfo("xmeshsaver_txt_fExtension",         maya.cmds.textField(_w.fExtension,          q=True, tx=True))

    if _w.channelList:
                            (cl,pcl) = getChannelStrings()
                            saveStringFileInfo("xmeshsaver_txt_channelList",        cl)
                            saveStringFileInfo("xmeshsaver_txt_proxyChannelList",   pcl)
    if _w.colorFromCurrentColorSet:
                            saveBoolFileInfo("xmeshsaver_chk_colorFromCurrentColorSet", maya.cmds.checkBoxGrp(_w.colorFromCurrentColorSet,  q=True, value1=True))
    if _w.map1FromCurrentUVSet:
                            saveBoolFileInfo("xmeshsaver_chk_map1FromCurrentUVSet", maya.cmds.checkBoxGrp(_w.map1FromCurrentUVSet,  q=True, value1=True))

    if _w.pathFrame:        saveBoolFileInfo("xmeshsaver_layout_pathFrame",         maya.cmds.frameLayout(_w.pathFrame,         q=True, cl=True))
    if _w.pathFrame:        saveBoolFileInfo("xmeshsaver_layout_frameFrame",            maya.cmds.frameLayout(_w.frameFrame,            q=True, cl=True))

    if _w.channelFrame:     saveBoolFileInfo("xmeshsaver_layout_channelFrame",      maya.cmds.frameLayout(_w.channelFrame,      q=True, cl=True))
    if _w.proxyFrame:       saveBoolFileInfo("xmeshsaver_layout_proxyFrame",        maya.cmds.frameLayout(_w.proxyFrame,        q=True, cl=True))
    if _w.preferencesFrame: saveBoolFileInfo("xmesh_layout_preferencesFrame",       maya.cmds.frameLayout(_w.preferencesFrame,  q=True, cl=True))


    if _w.subFolders:       saveBoolFileInfo("xmeshsaver_chk_subFolders",           maya.cmds.checkBox(_w.subFolders,           q=True, v=True))
    if _w.visibleOnly:      saveBoolFileInfo("xmeshsaver_chk_visibleOnly",          maya.cmds.checkBoxGrp(_w.visibleOnly,       q=True, value1=True))

    if _w.subFrames:        saveFloatFileInfo("xmeshsaver_flt_frameStep",           1.0 / frameSamplingOptions[maya.cmds.optionMenu(_w.subFrames, q=True, sl=True) - 1])
    if _w.meshMode:         saveIntFileInfo("xmeshsaver_om_meshMode",               getSettingsMeshMode(maya.cmds.optionMenu(_w.meshMode, q=True, sl=True)))
    if _w.loggingLevel:     saveIntFileInfo("xmeshsaver_int_loggingLevel",          maya.cmds.optionMenuGrp(_w.loggingLevel,    q=True, sl=True))


    if _w.startFrame:       saveIntFileInfo("xmeshsaver_int_startFrame",            maya.cmds.intField(_w.startFrame,           q=True,v=True))
    if _w.endFrame:         saveIntFileInfo("xmeshsaver_int_endFrame",              maya.cmds.intField(_w.endFrame,             q=True,v=True))

    if _w.useProxy:             saveBoolFileInfo("xmeshsaver_chk_useProxy",             maya.cmds.checkBox(_w.useProxy,             q=True,v=True))
    if _w.useProxySampling:     saveBoolFileInfo("xmeshsaver_chk_useProxySampling",     maya.cmds.checkBox(_w.useProxySampling,     q=True,v=True))
    if _w.proxyOptimization:    saveStringFileInfo("xmeshsaver_txt_proxyOptimization",  proxyOptimizationLabelToMode[maya.cmds.optionMenu(_w.proxyOptimization, q=True, v=True)])
    if _w.proxySubFrames:       saveFloatFileInfo("xmeshsaver_flt_proxyFrameStep",      1.0 / frameSamplingOptions[maya.cmds.optionMenu(_w.proxySubFrames, q=True, sl=True) - 1])
    if _w.proxyPercent:         saveFloatFileInfo("xmeshsaver_flt_proxyPercent",        maya.cmds.floatSliderGrp(_w.proxyPercent,   q=True,v=True))

    if gUseDeadline:

        if _wDL.jobName:        saveStringFileInfo("xmeshsaver_txt_dl_jobname",     maya.cmds.textFieldGrp(_wDL.jobName,    q=True,tx=True))
        if _wDL.comment:        saveStringFileInfo("xmeshsaver_txt_dl_comment",     maya.cmds.textFieldGrp(_wDL.comment,    q=True,tx=True))
        if _wDL.department:     saveStringFileInfo("xmeshsaver_txt_dl_department",  maya.cmds.textFieldGrp(_wDL.department, q=True,tx=True))

        if _wDL.pool and maya.cmds.optionMenuGrp(_wDL.pool, query=True, numberOfItems=True) > 0:
            s = maya.cmds.optionMenuGrp(_wDL.pool, q=True,v=True)
            if s:
                                saveStringFileInfo("xmeshsaver_enum_dl_pool", s)
        if _wDL.group and maya.cmds.optionMenuGrp(_wDL.group, query=True, numberOfItems=True) > 0:
            s = maya.cmds.optionMenuGrp(_wDL.group,q=True,v=True)
            if s:
                                saveStringFileInfo("xmeshsaver_enum_dl_group", s)

        if _wDL.machineLimit:   saveIntFileInfo("xmeshsaver_int_dl_machineLimit",   maya.cmds.intSliderGrp(_wDL.machineLimit, q=True, v=True))
        if _wDL.priority:       saveIntFileInfo("xmeshsaver_int_dl_priority",       maya.cmds.intSliderGrp(_wDL.priority, q=True, v=True))

        if _wDL.limitGroups:    saveStringFileInfo("xmeshsaver_txt_dl_limitGroups", sanitizeDeadlineList(maya.cmds.textFieldButtonGrp(_wDL.limitGroups, q=True, tx=True)))
        if _wDL.machineList:    saveStringFileInfo("xmeshsaver_txt_dl_machineList", sanitizeDeadlineList(maya.cmds.textFieldButtonGrp(_wDL.machineList, q=True, tx=True)))

        if _wDL.blacklistMode:  saveIntFileInfo("xmeshsaver_int_dl_blacklistMode",  maya.cmds.radioButtonGrp(_wDL.blacklistMode, q=True, sl=True))

        if _wDL.suspended:      saveBoolFileInfo("xmeshsaver_chk_dl_suspended",     maya.cmds.checkBoxGrp(_wDL.suspended,       q=True, value1=True))
        if _wDL.includeScene:   saveBoolFileInfo("xmeshsaver_chk_dl_includeScene",  maya.cmds.checkBoxGrp(_wDL.includeScene,    q=True, value1=True))

        if _wDL.taskCount:      saveIntFileInfo("xmeshsaver_int_dl_taskCount",      maya.cmds.intSliderGrp(_wDL.taskCount, q=True, v=True))

def loadPoolSetting():
    if gUseDeadline:
        s = "xmeshsaver_enum_dl_pool"
        if fileInfoExists(s):   selectInOptionMenu(_wDL.pool, getFileInfo(s))

def loadGroupSetting():
    if gUseDeadline:
        s = "xmeshsaver_enum_dl_group"
        if fileInfoExists(s):   selectInOptionMenu(_wDL.group, getFileInfo(s))

def loadSettings(*args):
    s = "xmeshsaver_txt_fpath"
    if fileInfoExists(s):   maya.cmds.textField(_w.fPath,       e=True, tx=getFileInfo(s))
    s = "xmeshsaver_txt_fRevision"
    if fileInfoExists(s):   maya.cmds.textField(_w.fRevision,   e=True, tx=getFileInfo(s))
    s = "xmeshsaver_txt_fProjectName"
    if fileInfoExists(s):   maya.cmds.textField(_w.fProjectName,e=True, tx=getFileInfo(s))
    s = "xmeshsaver_txt_fExtension"
    if fileInfoExists(s):   maya.cmds.textField(_w.fExtension,  e=True, tx=getFileInfo(s))
    ##
    s = "xmeshsaver_layout_pathFrame"
    if fileInfoExists(s):   maya.cmds.frameLayout(_w.pathFrame,     e=True, cl=getFileInfo(s))
    s = "xmeshsaver_layout_frameFrame"
    if fileInfoExists(s):   maya.cmds.frameLayout(_w.frameFrame,        e=True, cl=getFileInfo(s))
    s = "xmeshsaver_layout_channelFrame"
    if fileInfoExists(s):   maya.cmds.frameLayout(_w.channelFrame,  e=True, cl=getFileInfo(s))
    s = "xmeshsaver_layout_proxyFrame"
    if fileInfoExists(s):   maya.cmds.frameLayout(_w.proxyFrame,    e=True, cl=getFileInfo(s))
    s = "xmesh_layout_preferencesFrame"
    if fileInfoExists(s):   maya.cmds.frameLayout(_w.preferencesFrame,  e=True, cl=getFileInfo(s))
    ##
    s = "xmeshsaver_chk_subFolders"
    if fileInfoExists(s):   maya.cmds.checkBox(_w.subFolders,       e=True, v=getFileInfo(s))
    ##
    s = 'xmeshsaver_flt_frameStep'
    if fileInfoExists(s):
        val = getFileInfo(s)
        index = frameSamplingOptions.index(min(frameSamplingOptions, key=lambda x: abs(1.0/x-val)))
        maya.cmds.optionMenu(_w.subFrames, e=True, sl=(index + 1))
    else:
        s = "xmeshsaver_om_subFrames"
        if fileInfoExists(s):
            maya.cmds.optionMenu(_w.subFrames, e=True, sl=getFileInfo(s))
    s = "xmeshsaver_om_meshMode"
    if fileInfoExists(s):   maya.cmds.optionMenu(_w.meshMode,       e=True, sl=getControlMeshMode(getFileInfo(s)))
    s = "xmeshsaver_int_loggingLevel"
    if fileInfoExists(s):   maya.cmds.optionMenuGrp(_w.loggingLevel,        e=True, sl=getFileInfo(s))
    ##
    s = "xmeshsaver_int_startFrame"
    if fileInfoExists(s):   maya.cmds.intField(_w.startFrame,       e=True, v=getFileInfo(s))
    s = "xmeshsaver_int_endFrame"
    if fileInfoExists(s):   maya.cmds.intField(_w.endFrame,         e=True, v=getFileInfo(s))
    ##
    s = "xmeshsaver_chk_useProxy"
    if fileInfoExists(s):   maya.cmds.checkBox(_w.useProxy,         e=True, v=getFileInfo(s))
    s = "xmeshsaver_chk_useProxySampling"
    if fileInfoExists(s):   maya.cmds.checkBox(_w.useProxySampling, e=True, v=getFileInfo(s))
    s = "xmeshsaver_chk_visibleOnly"
    if fileInfoExists(s):   maya.cmds.checkBoxGrp(_w.visibleOnly,       e=True, v1=getFileInfo(s))
    s = "xmeshsaver_txt_proxyOptimization"
    if fileInfoExists(s):
        label = proxyOptimizationModeToLabel.get(getFileInfo(s), None)
        if label is not None:
            index = proxyOptimizationLabels.index(label)
            maya.cmds.optionMenu(_w.proxyOptimization, e=True, sl=(index + 1))
    s = 'xmeshsaver_flt_proxyFrameStep'
    if fileInfoExists(s):
        val = getFileInfo(s)
        index = frameSamplingOptions.index(min(frameSamplingOptions, key=lambda x: abs(1.0/x-val)))
        maya.cmds.optionMenu(_w.proxySubFrames, e=True, sl=(index + 1))
    else:
        s = "xmeshsaver_om_proxySubFrames"
        if fileInfoExists(s):
            maya.cmds.optionMenu(_w.proxySubFrames, e=True, sl=getFileInfo(s))

    s = "xmeshsaver_flt_proxyPercent"
    if fileInfoExists(s):   maya.cmds.floatSliderGrp(_w.proxyPercent,   e=True, v=getFileInfo(s))

    #channel allowlist
    s1 = "xmeshsaver_txt_channelList"
    s2 = "xmeshsaver_txt_proxyChannelList"
    cl = pcl = '~'
    if fileInfoExists(s1):
        cl = getFileInfo(s1)
    if fileInfoExists(s2):
        pcl = getFileInfo(s2)

    if cl != '~':
        if pcl == '~':
            pcl = cl
        setChannels(cl,pcl)

    s = "xmeshsaver_chk_colorFromCurrentColorSet"
    if fileInfoExists(s) and _w.colorFromCurrentColorSet:
        maya.cmds.checkBoxGrp(_w.colorFromCurrentColorSet, e=True, v1=getFileInfo(s))

    s = "xmeshsaver_chk_map1FromCurrentUVSet"
    if fileInfoExists(s):   maya.cmds.checkBoxGrp(_w.map1FromCurrentUVSet,      e=True, v1=getFileInfo(s))

    if gUseDeadline:
        s = "xmeshsaver_txt_dl_jobname"
        if fileInfoExists(s):   maya.cmds.textFieldGrp(_wDL.jobName,    e=True, tx=getFileInfo(s))
        s = "xmeshsaver_txt_dl_comment"
        if fileInfoExists(s):   maya.cmds.textFieldGrp(_wDL.comment,    e=True, tx=getFileInfo(s))
        s = "xmeshsaver_txt_dl_department"
        if fileInfoExists(s):   maya.cmds.textFieldGrp(_wDL.department, e=True, tx=getFileInfo(s))

        loadPoolSetting()
        loadGroupSetting()

        s = "xmeshsaver_int_dl_machineLimit"
        if fileInfoExists(s):   maya.cmds.intSliderGrp(_wDL.machineLimit,   e=True, v=getFileInfo(s))

        s = "xmeshsaver_int_dl_priority"
        if fileInfoExists(s):   maya.cmds.intSliderGrp(_wDL.priority,   e=True, v=getFileInfo(s))
        s = "xmeshsaver_txt_dl_limitGroups"
        if fileInfoExists(s):   maya.cmds.textFieldButtonGrp(_wDL.limitGroups,  e=True, tx=getFileInfo(s))
        s = "xmeshsaver_txt_dl_machineList"
        if fileInfoExists(s):   maya.cmds.textFieldButtonGrp(_wDL.machineList,  e=True, tx=getFileInfo(s))
        s = "xmeshsaver_int_dl_blacklistMode"
        if fileInfoExists(s):   maya.cmds.radioButtonGrp(_wDL.blacklistMode, e=True, sl=getFileInfo(s))

        s = "xmeshsaver_chk_dl_suspended"
        if fileInfoExists(s):   maya.cmds.checkBoxGrp(_wDL.suspended,   e=True, value1=getFileInfo(s))
        s = "xmeshsaver_chk_dl_includeScene"
        if fileInfoExists(s):   maya.cmds.checkBoxGrp(_wDL.includeScene,e=True, value1=getFileInfo(s))

        s = "xmeshsaver_int_dl_taskCount"
        if fileInfoExists(s):   maya.cmds.intSliderGrp(_wDL.taskCount,  e=True, v=getFileInfo(s))

def selectInOptionMenu(optionMenu, value, c=maya.cmds.optionMenuGrp):
    found = False
    numberOfItems = c(optionMenu, q=True, numberOfItems=True)
    for i in range(1, numberOfItems+1):
        c(optionMenu, e=True, select=i)
        if c(optionMenu, q=True, value=True) == value:
            found = True
            break
    if not found and numberOfItems > 0:
        c(optionMenu, e=True, select=1)

def getChannelProperties():
    channelProperties = {}
    if _w.colorFromCurrentColorSet and maya.cmds.checkBoxGrp(_w.colorFromCurrentColorSet, q=True, value1=True):
        channelProperties['Color'] = 'currentColorSet'
    if _w.map1FromCurrentUVSet and maya.cmds.checkBoxGrp(_w.map1FromCurrentUVSet, q=True, value1=True):
        channelProperties['TextureCoord'] = 'currentUVSet'
    return channelProperties

def getChannelLists():
    channelList = []
    proxyChannelList = []
    if _w.channelList:
        for i in range(0,len(xmChannels)):
            if maya.cmds.checkBoxGrp(_w.channelList[i], q=True, v1=True):
                channelList.extend(xmChannels[i].split(','))
            if maya.cmds.checkBoxGrp(_w.channelList[i], q=True, v2=True):
                proxyChannelList.extend(xmChannels[i].split(','))

    return (channelList,proxyChannelList)

def getChannelStrings():
    (channelList,proxyChannelList) = getChannelLists()
    return (','.join(channelList), ','.join(proxyChannelList))

def setChannels(channelString, proxyChannelString):
    channelSet = set(channelString.split(','))
    proxyChannelSet = set(proxyChannelString.split(','))

    for i in range(0,len(xmChannels)):
        maya.cmds.checkBoxGrp(_w.channelList[i], e=True, va2=[set(xmChannels[i].split(',')) <= channelSet,set(xmChannels[i].split(',')) <= proxyChannelSet])


def third_party_licenses(attr,node):
    if (attr and node):
        if not maya.cmds.objExists(node): return False
        if attr in maya.cmds.listAttr(node,shortNames=True): return True
        if attr in maya.cmds.listAttr(node): return True
    return False

#
def fileInfoExists(str):
    return attributeExists(str, getSettingsNode())

#GETTER
def getFileInfo(name):
    res = maya.cmds.getAttr(getSettingsNodeAttribute(name))
    return res

#BOOL
def saveBoolFileInfo(name, value):
    if not fileInfoExists(name):
        maya.cmds.addAttr(getSettingsNode(), ln=name,sn=name, at='bool')
    maya.cmds.setAttr(getSettingsNodeAttribute(name), value)
#INT
def saveIntFileInfo(name, value):
    if not fileInfoExists(name):
        maya.cmds.addAttr(getSettingsNode(), ln=name,sn=name, at='long')
    maya.cmds.setAttr(getSettingsNodeAttribute(name), value)
#FLOAT
def saveFloatFileInfo(name, value):
    if not fileInfoExists(name):
        maya.cmds.addAttr(getSettingsNode(), ln=name,sn=name, at='float')
    maya.cmds.setAttr(getSettingsNodeAttribute(name), value)
#STRING
def saveStringFileInfo(name, value):
    if not fileInfoExists(name):
        maya.cmds.addAttr(getSettingsNode(), ln=name,sn=name, dt='string')
    maya.cmds.setAttr(getSettingsNodeAttribute(name), value, type='string')
#
def getSettingsNode():
    nodeName = ':xmeshSaver_ui_settings_node'
    if not maya.cmds.objExists(nodeName):
        maya.cmds.createNode('xmeshSaverUISettings', s=True, name=nodeName, skipSelect=True)
    return nodeName
#
def getSettingsNodeAttribute(str):
    return getSettingsNode() + "." + str

#deadline UI
def setDeadlineList(control,command,c=maya.cmds.textFieldButtonGrp, *args):
    #SelectMachineList
    #SelectLimitGroups
    if control and gUseDeadline:
        items = sanitizeDeadlineList(c(control, q=True, text=True))
        res = deadlineCommand([command, items]) #'%s %s' % (command, ','.join(initialList)))
        if (res is not None) and (not res.startswith(b'Action was cancelled')):
            c(control, e=True, text=res)
            saveSettings()

def sanitizeDeadlineList(itemString):
    itemString = itemString.replace(', ', ',')
    itemString = itemString.replace('\\', '')
    itemString = itemString.replace(';',',')
    return itemString

def setJobName(control, c=maya.cmds.textFieldButtonGrp,*args):
    if control:
        fn = maya.cmds.file(q=True,sn=True,shn=True)
        c(control,e=True,tx=fn)


def deadlineCommand(cmd="", *args):
    try:
        return DL.safeDeadlineCommand(cmd)
    except DL.DeadlineError as e:
        print("// XMesh: Deadline error:", str(e))
        return None

def memoizedDeadlineCommand(cmd="", *args):
    result = _gDeadlineCommandCache.get(cmd, None)
    if result is None:
        result = deadlineCommand(cmd, args)
        _gDeadlineCommandCache[cmd] = result
    return result

#maya validation
def isMesh(path):
    try:
        fnMesh = maya.OpenMaya.MFnMesh()
        fnMesh.setObject(path)
        return True
    except:
        return False

def canSave(objectName):
    selectionList = maya.OpenMaya.MSelectionList()
    selectionList.add(objectName)
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
                return True
    return False

def pluginVersion():
    if maya.cmds.pluginInfo('XMesh', query=True, loaded=True):
        return maya.cmds.pluginInfo('XMesh', query=True, version=True)
    else:
        return "Not Loaded"

def populateOptionMenuGrpFromDeadline(control, deadlineArgument):
    numberOfItems = maya.cmds.optionMenuGrp(control, query=True, numberOfItems=True)
    if numberOfItems > 0:
        return

    savedValue = b"none"

    #Read in the pools.
    deadlineResultRaw = memoizedDeadlineCommand(deadlineArgument)
    if deadlineResultRaw is not None:
        deadlineResult = deadlineResultRaw.split()
    else:
        deadlineResult = ['none']

    maya.cmds.setParent(control)
    for i in range(0, len(deadlineResult)):
        maya.cmds.menuItem(parent=control+'|OptionMenu', l=deadlineResult[i])
        if deadlineResult[i] == savedValue:
            maya.cmds.optionMenuGrp(control, e=True, select=i+1)

def populatePoolControl():
    populateOptionMenuGrpFromDeadline(_wDL.pool, '-pools')

def populateGroupControl():
    populateOptionMenuGrpFromDeadline(_wDL.group, '-groups')

def onDeadlineFramePreExpand():
    if gUseDeadline:
        with waitCursor():
            populatePoolControl()
            loadPoolSetting()

            populateGroupControl()
            loadGroupSetting()

def clamp(value, minValue, maxValue):
    return max(minValue, min(value, maxValue))

def getSettingsMeshMode(controlMeshMode):
    lut = [1, 3, 2]
    return lut[clamp(controlMeshMode - 1, 0, len(lut) - 1)]

def getControlMeshMode(settingsMeshMode):
    lut = [1, 3, 2]
    return lut[clamp(settingsMeshMode - 1, 0, len(lut) - 1)]
