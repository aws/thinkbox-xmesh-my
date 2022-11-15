# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0

import maya.cmds as cmds
import os

xmeshmyNoticesWindowId = 'xmeshmyNoticesDlg'
xmeshmyNoticesText = None
xmeshmyNoticesError = "Could not read third_party_licenses.txt"

def open_xmeshmy_notices_dialog():
    global xmeshmyNoticesWindowId
    global xmeshmyNoticesText
    global xmeshmyNoticesError

    if cmds.window( xmeshmyNoticesWindowId, exists=True ):
        cmds.deleteUI( xmeshmyNoticesWindowId )

    if xmeshmyNoticesText == None or xmeshmyNoticesText == xmeshmyNoticesError :
        noticesPath = os.path.join( os.path.dirname( __file__ ), '..', '..', 'Legal', 'third_party_licenses.txt' )
        try:
            with open( noticesPath, 'r' ) as theFile:
                xmeshmyNoticesText = theFile.read()
        except IOError:
            xmeshmyNoticesText = xmeshmyNoticesError

    cmds.window( xmeshmyNoticesWindowId, title="XMesh MY Notices", width=516 )

    cmds.formLayout( "resizeForm" )

    sf = cmds.scrollField( wordWrap=True, text=xmeshmyNoticesText, editable=False )

    cmds.formLayout( "resizeForm", edit=True, attachForm=[( sf, 'top', 0 ), ( sf, 'right', 0 ), ( sf, 'left', 0 ), ( sf, 'bottom', 0 )] )

    cmds.setParent( '..' )

    cmds.showWindow()