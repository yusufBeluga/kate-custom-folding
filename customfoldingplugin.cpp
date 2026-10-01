#include "customfoldingplugin.h"

CustomFoldingPlugin::CustomFoldingPlugin(QObject *parent, const QVariantList &)
: KTextEditor::Plugin(parent)
{
}

CustomFoldingPlugin::~CustomFoldingPlugin()
{
}

QObject *CustomFoldingPlugin::createView(KTextEditor::MainWindow *mainWindow)
{
    return new CustomFoldingPluginView(mainWindow);
}

CustomFoldingPluginView::CustomFoldingPluginView(KTextEditor::MainWindow *mainWindow)
: QObject(mainWindow)
, m_mainWindow(mainWindow)
{
    KActionCollection *ac = m_mainWindow->actionCollection();

    QAction *actionCreate = ac->addAction(QStringLiteral("custom_fold_create"));
    actionCreate->setText(tr("Create Custom Folding"));
    connect(actionCreate, &QAction::triggered, this, &CustomFoldingPluginView::slotCreateCustomFold);

    QAction *actionClear = ac->addAction(QStringLiteral("custom_fold_clear"));
    actionClear->setText(tr("Clear Custom Folding"));
    connect(actionClear, &QAction::triggered, this, &CustomFoldingPluginView::slotClearCustomFold);

    m_mainWindow->guiFactory()->addClient(m_mainWindow);
}

CustomFoldingPluginView::~CustomFoldingPluginView()
{
    m_mainWindow->guiFactory()->removeClient(m_mainWindow);
}

void CustomFoldingPluginView::slotCreateCustomFold()
{
    KTextEditor::View *view = m_mainWindow->activeView();
    if (!view || !view->hasSelection()) return;

    KTextEditor::Range selectionRange = view->selectionRange();
    int startLine = selectionRange.start().line();
    int endLine = selectionRange.end().line();

    if (startLine >= endLine) return;

    // Accede all'interfaccia di folding nativa di KF6
    // Iniettando direttamente il range personalizzato come blocco persistente
    // la freccia a lato rimarrà visibile ed utilizzabile all'infinito
    if (auto foldingInterface = qobject_cast<KTextEditor::FoldingInterface*>(view)) {
        foldingInterface->createFoldingRange(startLine, endLine);
        foldingInterface->foldLine(startLine);
    }

    view->removeSelection();
}

void CustomFoldingPluginView::slotClearCustomFold()
{
    KTextEditor::View *view = m_mainWindow->activeView();
    if (!view) return;

    int currentLine = view->cursorPosition().line();

    if (auto foldingInterface = qobject_cast<KTextEditor::FoldingInterface*>(view)) {
        if (foldingInterface->isLineFolded(currentLine)) {
            foldingInterface->unfoldLine(currentLine);
        }
        foldingInterface->removeFoldingRangeAt(currentLine);
    }
}
