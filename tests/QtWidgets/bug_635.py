#############################################################################
##
## Copyright (C) 2016 The Qt Company Ltd.
## Contact: https://www.qt.io/licensing/
##
## This file is part of the test suite of PySide2.
##
## $QT_BEGIN_LICENSE:GPL-EXCEPT$
## Commercial License Usage
## Licensees holding valid commercial Qt licenses may use this file in
## accordance with the commercial license agreement provided with the
## Software or, alternatively, in accordance with the terms contained in
## a written agreement between you and The Qt Company. For licensing terms
## and conditions see https://www.qt.io/terms-conditions. For further
## information use the contact form at https://www.qt.io/contact-us.
##
## GNU General Public License Usage
## Alternatively, this file may be used under the terms of the GNU
## General Public License version 3 as published by the Free Software
## Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
## included in the packaging of this file. Please review the following
## information to ensure the GNU General Public License requirements will
## be met: https://www.gnu.org/licenses/gpl-3.0.html.
##
## $QT_END_LICENSE$
##
#############################################################################

''' Test bug 635: http://bugs.openbossa.org/show_bug.cgi?id=635'''

import unittest
from PySide2.QtGui import QIcon
from PySide2.QtWidgets import QApplication, QToolBar
import sys

class testQToolBar(unittest.TestCase):
    def callback(self):
        self._called = True

    def testAddAction(self):
        bar = QToolBar()
        self._called = False
        a = bar.addAction("act1", self.callback)
        a.trigger()
        self.assert_(self._called)

    def testAddActionWithIcon(self):
        bar = QToolBar()
        self._called = False
        icon = QIcon()
        a = bar.addAction(icon, "act1", self.callback)
        a.trigger()
        self.assert_(self._called)

if __name__ == '__main__':
    app = QApplication(sys.argv)
    unittest.main()
