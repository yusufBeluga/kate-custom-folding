#pragma once

#include <KTextEditor/Document>
#include <KTextEditor/MainWindow>
#include <KTextEditor/MovingRange>
#include <KTextEditor/Plugin>
#include <KTextEditor/View>

#include <KXMLGUIClient>

#include <QHash>
#include <QPointer>
#include <QUrl>
#include <QVariantList>

#include <memory>
#include <vector>

class QTimer;

class CustomFoldingPlugin : public KTextEditor::Plugin
{
    Q_OBJECT
public:
    explicit CustomFoldingPlugin(QObject *parent, const QVariantList & = QVariantList());
    ~CustomFoldingPlugin() override;

    QObject *createView(KTextEditor::MainWindow *mainWindow) override;
};

/**
 * Stato per ogni KTextEditor::View:
 * - i range creati dall'utente (MovingRange: seguono le modifiche al testo)
 * - il widget "KateIconBorder" (bordo con le frecce di folding) su cui
 *   intercettiamo i click
 * - un timer per risincronizzare i range con il folding della view
 */
struct CustomFoldViewState {
    QPointer<KTextEditor::View> view;
    QPointer<KTextEditor::Document> doc;
    QPointer<QWidget> iconBorder;
    QTimer *syncTimer = nullptr;
    std::vector<std::unique_ptr<KTextEditor::MovingRange>> ranges;
    QHash<KTextEditor::MovingRange *, bool> folded; // ultimo stato noto (contratto?)
    QUrl url; // file a cui appartengono i range salvati
    bool restored = false; // true dopo il primo ripristino: prima non salviamo nulla
};

class CustomFoldingPluginView : public QObject, public KXMLGUIClient
{
    Q_OBJECT
public:
    explicit CustomFoldingPluginView(KTextEditor::MainWindow *mainWindow);
    ~CustomFoldingPluginView() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class Op { None, Fold, Toggle, Remove };

    void slotCreateCustomFold();
    void slotClearCustomFold();

    void attachView(KTextEditor::View *view);
    void detachView(QObject *view);
    CustomFoldViewState *stateFor(KTextEditor::View *view);

    /**
     * Allinea il folding della view con i range dell'estensione.
     * Se target != nullptr applica op a quel range.
     */
    void sync(CustomFoldViewState &st,
              KTextEditor::MovingRange *target = nullptr,
              Op op = Op::None,
              const QHash<KTextEditor::MovingRange *, bool> *forced = nullptr);

    /// Ripristina i range salvati per il file del documento
    void restore(CustomFoldViewState &st);
    /// Salva su disco i range del documento
    void persist(CustomFoldViewState &st);
    void clearRanges(CustomFoldViewState &st);

    KTextEditor::MovingRange *rangeStartingAt(CustomFoldViewState &st, int line) const;
    bool isInFoldingArea(QWidget *border, KTextEditor::View *view, const QPoint &pos) const;
    int lineAt(QWidget *border, KTextEditor::View *view, const QPoint &pos) const;
    void showMessage(KTextEditor::View *view, const QString &text);

    KTextEditor::MainWindow *m_mainWindow;
    QHash<QObject *, CustomFoldViewState *> m_states; // key: View
    QHash<QObject *, CustomFoldViewState *> m_borders; // key: KateIconBorder
};
