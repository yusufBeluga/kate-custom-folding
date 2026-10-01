#pragma once
#include <QObject>
#include <QString>
#include <QAction>

namespace KTextEditor {

class Document : public QObject { Q_OBJECT };

class Range {
public:
    struct Pos { int line() const { return 0; } };
    Pos start() const { return Pos(); }
    Pos end() const { return Pos(); }
    static Range invalid() { return Range(); }
};

class View : public QObject {
    Q_OBJECT
public:
    bool hasSelection() const { return true; }
    Range selectionRange() const { return Range(); }
    void removeSelection() {}
    struct Cursor { int line() const { return 0; } };
    Cursor cursorPosition() const { return Cursor(); }
    Document* document() const { return nullptr; }
};

class FoldingInterface {
public:
    virtual void createFoldingRange(int, int) = 0;
    virtual void foldLine(int) = 0;
    virtual bool isLineFolded(int) = 0;
    virtual void unfoldLine(int) = 0;
    virtual void removeFoldingRangeAt(int) = 0;
};

class MainWindow : public QObject {
    Q_OBJECT
public:
    View* activeView() const { return nullptr; }
    class ActionCollection {
    public:
        // Cambiato il tipo di ritorno da QAction* a ActionCollection* per la cascata di metodi
        ActionCollection* addAction(const QString&) { return this; }
        void setText(const QString&) {}
    };
    ActionCollection* actionCollection() const { return new ActionCollection(); }
    class XMLGuiFactory {
    public:
        void addClient(QObject*) {}
        void removeClient(QObject*) {}
    };
    XMLGuiFactory* guiFactory() const { return new XMLGuiFactory(); }
};

class Plugin : public QObject {
    Q_OBJECT
public:
    Plugin(QObject* parent = nullptr) : QObject(parent) {}
    virtual QObject* createView(MainWindow*) = 0;
};

}

#define tr(txt) QStringLiteral(txt)
#define QStringLiteral(txt) QString(txt)
