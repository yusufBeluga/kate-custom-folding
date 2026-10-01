#pragma once
// Sostituisci le vecchie inclusioni <KTextEditor/...> con questa:
#include "KTextEditor_Stub.h" 

#include <QObject>
#include <QVariantList>

class CustomFoldingPlugin : public KTextEditor::Plugin
{
    Q_OBJECT
public:
    explicit CustomFoldingPlugin(QObject *parent = nullptr, const QVariantList & = QVariantList());
    ~CustomFoldingPlugin() override;

    QObject *createView(KTextEditor::MainWindow *mainWindow) override;
};

class CustomFoldingPluginView : public QObject
{
    Q_OBJECT
public:
    explicit CustomFoldingPluginView(KTextEditor::MainWindow *mainWindow);
    ~CustomFoldingPluginView() override;

private slots:
    void slotCreateCustomFold();
    void slotClearCustomFold();

private:
    KTextEditor::MainWindow *m_mainWindow;
};
