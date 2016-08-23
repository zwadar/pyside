#!/usr/bin/python

import unittest

from PySide2 import QtCore
from PySide2 import QtGui
from PySide2 import QtWidgets
from helper import UsesQApplication

class Item(QtWidgets.QGraphicsItem):
    
    def __init__(self):
        QtWidgets.QGraphicsItem.__init__(self)

    def boundingRect(self):
        return QtCore.QRectF(0, 0, 100, 100)
    
    def paint(self, painter, option, widget):
        painter.setBrush(QtGui.QColor(255, 255, 255))
        painter.drawRect(0, 0, 100, 100)


class QGraphicsViewIsBlockedTest(UsesQApplication):

    def testIsBlockedByModalPanel(self):
        (first, second) = Item().isBlockedByModalPanel()
        self.assertFalse(first)

if __name__ == "__main__":
    unittest.main()