#include "customfoldingplugin.h"

#include <KTextEditor/Document>
#include <KTextEditor/Message>

#include <KActionCollection>
#include <KConfig>
#include <KConfigGroup>
#include <KPluginFactory>
#include <KXMLGUIFactory>

#include <QAction>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QFontMetrics>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QMouseEvent>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <climits>
#include <limits>

K_PLUGIN_FACTORY_WITH_JSON(CustomFoldingPluginFactory, "customfoldingplugin.json", registerPlugin<CustomFoldingPlugin>();)

namespace
{
// Valori di Kate::TextFolding::FoldingRangeFlag (formato JSON della sessione di Kate)
constexpr int FlagPersistent = 0x1;
constexpr int FlagFolded = 0x2;

struct FoldEntry {
    int startLine = 0;
    int startColumn = 0;
    int endLine = 0;
    int endColumn = 0;
    int flags = 0;
    bool ours = false;
};

int startLineOf(const KTextEditor::MovingRange *r)
{
    return r->start().line();
}

int endLineOf(const KTextEditor::MovingRange *r)
{
    return r->end().line();
}

bool isUsable(const KTextEditor::MovingRange *r)
{
    const KTextEditor::Range range = r->toRange();
    return range.isValid() && range.end().line() > range.start().line();
}

KTextEditor::Range lineRange(KTextEditor::Document *doc, int startLine, int endLine)
{
    return KTextEditor::Range(KTextEditor::Cursor(startLine, 0), KTextEditor::Cursor(endLine, doc->lineLength(endLine)));
}

// ---------------------------------------------------------------------------
// Archivio su disco: un file JSON con i range di ogni file aperto in Kate
//   { "files": { "<url>": { "time": <ms>, "ranges": [ {start, end, folded, startHash, endHash} ] } } }
// L'hash del testo della prima e dell'ultima riga serve a non ripristinare
// range su righe sbagliate se il file è stato modificato fuori da Kate.
// ---------------------------------------------------------------------------

constexpr int MaxStoredFiles = 500;

QString storePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/customfolding-ranges.json");
}

QJsonObject loadStore()
{
    QFile f(storePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(f.readAll()).object();
}

void writeStore(const QJsonObject &root)
{
    QSaveFile f(storePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

QString lineHash(KTextEditor::Document *doc, int line)
{
    return QString::fromLatin1(QCryptographicHash::hash(doc->line(line).toUtf8(), QCryptographicHash::Sha1).toHex().left(16));
}
}

// ---------------------------------------------------------------------------
// Plugin
// ---------------------------------------------------------------------------

CustomFoldingPlugin::CustomFoldingPlugin(QObject *parent, const QVariantList &)
    : KTextEditor::Plugin(parent)
{
}

CustomFoldingPlugin::~CustomFoldingPlugin() = default;

QObject *CustomFoldingPlugin::createView(KTextEditor::MainWindow *mainWindow)
{
    return new CustomFoldingPluginView(mainWindow);
}

// ---------------------------------------------------------------------------
// Plugin view (una per finestra principale di Kate)
// ---------------------------------------------------------------------------

CustomFoldingPluginView::CustomFoldingPluginView(KTextEditor::MainWindow *mainWindow)
    : QObject(mainWindow)
    , m_mainWindow(mainWindow)
{
    KXMLGUIClient::setComponentName(QStringLiteral("customfoldingplugin"), QStringLiteral("Custom Folding"));
    setXMLFile(QStringLiteral("ui.rc"));

    QAction *create = actionCollection()->addAction(QStringLiteral("custom_fold_create"));
    create->setText(QStringLiteral("Crea folding sulle righe selezionate"));
    KActionCollection::setDefaultShortcut(create, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Comma));
    connect(create, &QAction::triggered, this, &CustomFoldingPluginView::slotCreateCustomFold);

    QAction *clear = actionCollection()->addAction(QStringLiteral("custom_fold_clear"));
    clear->setText(QStringLiteral("Elimina folding personalizzato"));
    KActionCollection::setDefaultShortcut(clear, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Period));
    connect(clear, &QAction::triggered, this, &CustomFoldingPluginView::slotClearCustomFold);

    m_mainWindow->guiFactory()->addClient(this);

    const auto views = m_mainWindow->views();
    for (KTextEditor::View *view : views) {
        attachView(view);
    }
    connect(m_mainWindow, &KTextEditor::MainWindow::viewCreated, this, &CustomFoldingPluginView::attachView);
}

CustomFoldingPluginView::~CustomFoldingPluginView()
{
    m_mainWindow->guiFactory()->removeClient(this);

    for (CustomFoldViewState *st : std::as_const(m_states)) {
        if (st->iconBorder) {
            st->iconBorder->removeEventFilter(this);
        }
        if (st->view) {
            disconnect(st->view, nullptr, this, nullptr);
        }
        if (st->doc) {
            disconnect(st->doc, nullptr, this, nullptr);
        }
        persist(*st);
        delete st;
    }
    m_states.clear();
    m_borders.clear();
}

void CustomFoldingPluginView::attachView(KTextEditor::View *view)
{
    if (!view || m_states.contains(view)) {
        return;
    }

    auto *st = new CustomFoldViewState;
    st->view = view;
    st->doc = view->document();
    m_states.insert(view, st);

    // Il bordo con le frecce di folding è un widget interno di KTextEditor
    // ("KateIconBorder"): lo cerchiamo per nome di classe, senza usare API private.
    const auto children = view->findChildren<QWidget *>();
    for (QWidget *w : children) {
        if (qstrcmp(w->metaObject()->className(), "KateIconBorder") == 0) {
            st->iconBorder = w;
            m_borders.insert(w, st);
            w->installEventFilter(this);
            break;
        }
    }

    // Quando Kate apre un range (click sulla freccia, sul "..." o con le scorciatoie
    // native) un range non persistente viene eliminato: lo ricreiamo subito.
    st->syncTimer = new QTimer(this);
    st->syncTimer->setSingleShot(true);
    st->syncTimer->setInterval(30);
    connect(st->syncTimer, &QTimer::timeout, this, [this, view]() {
        if (CustomFoldViewState *s = m_states.value(view)) {
            sync(*s);
        }
    });

    auto schedule = [st]() {
        if (!st->ranges.empty()) {
            st->syncTimer->start();
        }
    };
    connect(view, &KTextEditor::View::cursorPositionChanged, this, schedule);
    connect(view, &KTextEditor::View::displayRangeChanged, this, schedule);

    QPointer<KTextEditor::View> viewGuard(view);
    KTextEditor::Document *doc = view->document();
    auto withState = [this, viewGuard](auto fn) {
        return [this, viewGuard, fn]() {
            if (CustomFoldViewState *s = m_states.value(viewGuard.data())) {
                fn(*s);
            }
        };
    };

    // chiusura / ricarica / apertura di un altro file nel documento:
    // il contenuto è ancora valido, quindi salviamo e poi buttiamo i range
    connect(doc, &KTextEditor::Document::aboutToInvalidateMovingInterfaceContent, this, withState([this](CustomFoldViewState &s) {
                persist(s);
                clearRanges(s);
            }));
    connect(doc, &KTextEditor::Document::aboutToClose, this, withState([this](CustomFoldViewState &s) {
                persist(s);
            }));
    connect(doc, &KTextEditor::Document::documentSavedOrUploaded, this, withState([this](CustomFoldViewState &s) {
                persist(s);
            }));
    // dopo una ricarica: ripristiniamo dal file
    connect(doc, &KTextEditor::Document::reloaded, this, withState([this](CustomFoldViewState &s) {
                restore(s);
            }));
    // "Salva con nome" (i range restano, cambia il file) o apertura di un altro file
    connect(doc, &KTextEditor::Document::documentUrlChanged, this, withState([this](CustomFoldViewState &s) {
                if (!s.ranges.empty()) {
                    s.url = s.doc ? s.doc->url() : QUrl();
                    persist(s);
                } else {
                    restore(s);
                }
            }));

    connect(view, &QObject::destroyed, this, &CustomFoldingPluginView::detachView);

    // Ripristino dei range salvati. Leggero ritardo: Kate, riaprendo la sessione,
    // applica il proprio stato alla view subito dopo averla creata.
    QTimer::singleShot(300, this, withState([this](CustomFoldViewState &s) {
                           restore(s);
                       }));
}

void CustomFoldingPluginView::detachView(QObject *view)
{
    CustomFoldViewState *st = m_states.take(view);
    if (!st) {
        return;
    }
    for (auto it = m_borders.begin(); it != m_borders.end();) {
        if (it.value() == st) {
            it = m_borders.erase(it);
        } else {
            ++it;
        }
    }
    if (st->syncTimer) {
        st->syncTimer->stop();
        st->syncTimer->deleteLater();
    }
    persist(*st);
    delete st;
}

CustomFoldViewState *CustomFoldingPluginView::stateFor(KTextEditor::View *view)
{
    if (!view) {
        return nullptr;
    }
    if (!m_states.contains(view)) {
        attachView(view);
    }
    return m_states.value(view);
}

// ---------------------------------------------------------------------------
// Scorciatoie
// ---------------------------------------------------------------------------

void CustomFoldingPluginView::slotCreateCustomFold()
{
    KTextEditor::View *view = m_mainWindow->activeView();
    CustomFoldViewState *st = stateFor(view);
    if (!st) {
        return;
    }

    if (!view->selection()) {
        showMessage(view, QStringLiteral("Seleziona almeno due righe da raggruppare."));
        return;
    }

    const KTextEditor::Range sel = view->selectionRange();
    const int startLine = sel.start().line();
    int endLine = sel.end().line();
    // selezione per righe intere (Shift+Giù): l'ultima riga è quella prima del cursore a colonna 0
    if (sel.end().column() == 0 && endLine > startLine) {
        --endLine;
    }
    if (endLine <= startLine) {
        showMessage(view, QStringLiteral("Seleziona almeno due righe da raggruppare."));
        return;
    }

    // esiste già? allora la contraiamo e basta
    for (const auto &r : st->ranges) {
        if (isUsable(r.get()) && startLineOf(r.get()) == startLine && endLineOf(r.get()) == endLine) {
            view->removeSelection();
            sync(*st, r.get(), Op::Fold);
            return;
        }
    }

    // Le regioni di folding possono essere annidate ma non sovrapporsi parzialmente
    for (const auto &r : st->ranges) {
        if (!isUsable(r.get())) {
            continue;
        }
        const int s = startLineOf(r.get());
        const int e = endLineOf(r.get());
        const bool disjoint = endLine < s || startLine > e;
        const bool nested = (startLine >= s && endLine <= e) || (s >= startLine && e <= endLine);
        if (!disjoint && !nested) {
            showMessage(view, QStringLiteral("Il range si sovrappone parzialmente a un altro folding personalizzato."));
            return;
        }
    }

    KTextEditor::Document *doc = view->document();
    KTextEditor::MovingRange *range =
        doc->newMovingRange(lineRange(doc, startLine, endLine), KTextEditor::MovingRange::DoNotExpand, KTextEditor::MovingRange::InvalidateIfEmpty);
    st->ranges.emplace_back(range);

    view->removeSelection();
    sync(*st, range, Op::Fold);
}

void CustomFoldingPluginView::slotClearCustomFold()
{
    KTextEditor::View *view = m_mainWindow->activeView();
    CustomFoldViewState *st = stateFor(view);
    if (!st) {
        return;
    }

    const int line = view->cursorPosition().line();

    // priorità al range che inizia sulla riga del cursore, altrimenti il più interno che la contiene
    KTextEditor::MovingRange *target = rangeStartingAt(*st, line);
    if (!target) {
        int bestSpan = INT_MAX;
        for (const auto &r : st->ranges) {
            if (!isUsable(r.get())) {
                continue;
            }
            const int s = startLineOf(r.get());
            const int e = endLineOf(r.get());
            if (line >= s && line <= e && (e - s) < bestSpan) {
                bestSpan = e - s;
                target = r.get();
            }
        }
    }

    if (!target) {
        showMessage(view, QStringLiteral("Nessun folding personalizzato sulla riga corrente."));
        return;
    }

    // eliminiamo lo stesso range anche nelle altre viste dello stesso documento
    const int s = startLineOf(target);
    const int e = endLineOf(target);
    const auto states = m_states.values();
    for (CustomFoldViewState *other : states) {
        if (other == st || other->doc != st->doc) {
            continue;
        }
        for (const auto &r : other->ranges) {
            if (isUsable(r.get()) && startLineOf(r.get()) == s && endLineOf(r.get()) == e) {
                sync(*other, r.get(), Op::Remove);
                break;
            }
        }
    }

    sync(*st, target, Op::Remove);
}

// ---------------------------------------------------------------------------
// Click sulla freccia di folding
// ---------------------------------------------------------------------------

bool CustomFoldingPluginView::eventFilter(QObject *watched, QEvent *event)
{
    const QEvent::Type type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseButtonRelease && type != QEvent::MouseButtonDblClick) {
        return QObject::eventFilter(watched, event);
    }

    CustomFoldViewState *st = m_borders.value(watched);
    if (!st || !st->view || st->ranges.empty()) {
        return QObject::eventFilter(watched, event);
    }

    auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() != Qt::LeftButton) {
        return QObject::eventFilter(watched, event);
    }

    auto *border = static_cast<QWidget *>(watched);
    const QPoint pos = me->position().toPoint();
    if (!isInFoldingArea(border, st->view, pos)) {
        return QObject::eventFilter(watched, event);
    }

    KTextEditor::MovingRange *range = rangeStartingAt(*st, lineAt(border, st->view, pos));
    if (!range) {
        // freccia di un folding "normale" (sintassi): la gestisce Kate
        return QObject::eventFilter(watched, event);
    }

    // È la freccia di un nostro range: consumiamo press/doppio click e
    // facciamo il toggle al rilascio. Kate non riceve l'evento, quindi
    // non prova a contrarre la regione di sintassi che contiene la riga.
    if (type == QEvent::MouseButtonRelease) {
        QPointer<KTextEditor::View> view = st->view;
        QTimer::singleShot(0, this, [this, view, range]() {
            CustomFoldViewState *s = m_states.value(view.data());
            if (!s) {
                return;
            }
            // il range potrebbe essere stato rimosso nel frattempo
            for (const auto &r : s->ranges) {
                if (r.get() == range) {
                    sync(*s, range, Op::Toggle);
                    return;
                }
            }
        });
    }
    return true;
}

bool CustomFoldingPluginView::isInFoldingArea(QWidget *border, KTextEditor::View *view, const QPoint &pos) const
{
    if (!view->configValue(QStringLiteral("folding-bar")).toBool()) {
        return false;
    }
    // L'area delle frecce è l'ultima colonna a destra del bordo ed è larga
    // quanto l'altezza del font dell'editor.
    const QFont font = view->configValue(QStringLiteral("font")).value<QFont>();
    const int foldingWidth = qMax(10, QFontMetrics(font).height());
    return pos.x() >= border->width() - foldingWidth - 3;
}

int CustomFoldingPluginView::lineAt(QWidget *border, KTextEditor::View *view, const QPoint &pos) const
{
    QPoint p = border->mapTo(view, pos);
    p.setX(view->textAreaRect().left() + 1);
    const KTextEditor::Cursor c = view->coordinatesToCursor(p);
    return c.isValid() ? c.line() : -1;
}

KTextEditor::MovingRange *CustomFoldingPluginView::rangeStartingAt(CustomFoldViewState &st, int line) const
{
    if (line < 0) {
        return nullptr;
    }
    // se più range partono dalla stessa riga, prendiamo il più esterno
    KTextEditor::MovingRange *best = nullptr;
    for (const auto &r : st.ranges) {
        if (isUsable(r.get()) && startLineOf(r.get()) == line) {
            if (!best || endLineOf(r.get()) > endLineOf(best)) {
                best = r.get();
            }
        }
    }
    return best;
}

void CustomFoldingPluginView::showMessage(KTextEditor::View *view, const QString &text)
{
    auto *msg = new KTextEditor::Message(text, KTextEditor::Message::Information);
    msg->setView(view);
    msg->setAutoHide(2500);
    msg->setPosition(KTextEditor::Message::BottomInView);
    view->document()->postMessage(msg);
}

// ---------------------------------------------------------------------------
// Sincronizzazione con il folding della view
//
// KTextEditor non espone un'API pubblica per creare regioni di folding.
// L'unica via pubblica è lo stato di sessione della view
// (writeSessionConfig / readSessionConfig), che contiene la chiave
// "TextFolding": un JSON con tutte le regioni e i loro flag.
//   - flag Persistent (1): regione aperta che NON sparisce -> la freccia resta
//   - flag Folded (2):     regione contratta
// Leggiamo lo stato, aggiungiamo/modifichiamo le nostre regioni e lo
// reimportiamo. Una regione contratta importata perde il flag Persistent,
// quindi quando Kate la riapre la elimina: sync() se ne accorge e la
// ricrea subito come regione aperta persistente, così la freccia rimane.
// ---------------------------------------------------------------------------

void CustomFoldingPluginView::sync(CustomFoldViewState &st,
                                   KTextEditor::MovingRange *target,
                                   Op op,
                                   const QHash<KTextEditor::MovingRange *, bool> *forced)
{
    KTextEditor::View *view = st.view;
    if (!view) {
        return;
    }
    KTextEditor::Document *doc = view->document();

    // scarta i range diventati vuoti / su una riga sola dopo le modifiche
    st.ranges.erase(std::remove_if(st.ranges.begin(),
                                   st.ranges.end(),
                                   [&st, target](const std::unique_ptr<KTextEditor::MovingRange> &r) {
                                       const bool drop = r.get() != target && !isUsable(r.get());
                                       if (drop) {
                                           st.folded.remove(r.get());
                                       }
                                       return drop;
                                   }),
                    st.ranges.end());
    if (target && !isUsable(target)) {
        op = Op::Remove;
    }

    KConfig config(QString(), KConfig::SimpleConfig); // solo in memoria
    KConfigGroup group(&config, QStringLiteral("CustomFolding"));
    view->writeSessionConfig(group);

    const QJsonObject root = QJsonDocument::fromJson(group.readEntry("TextFolding", QByteArray())).object();
    const QJsonArray current = root.value(QStringLiteral("ranges")).toArray();

    auto key = [](int s, int e) {
        return (qint64(s) << 32) | quint32(e);
    };

    QHash<qint64, KTextEditor::MovingRange *> ourKeys;
    for (const auto &r : st.ranges) {
        if (isUsable(r.get())) {
            ourKeys.insert(key(startLineOf(r.get()), endLineOf(r.get())), r.get());
        }
    }

    std::vector<FoldEntry> entries;
    QHash<KTextEditor::MovingRange *, int> currentFlags; // presenti nello stato attuale
    for (const QJsonValue &v : current) {
        const QJsonObject o = v.toObject();
        FoldEntry e;
        e.startLine = o.value(QStringLiteral("startLine")).toInt();
        e.startColumn = o.value(QStringLiteral("startColumn")).toInt();
        e.endLine = o.value(QStringLiteral("endLine")).toInt();
        e.endColumn = o.value(QStringLiteral("endColumn")).toInt();
        e.flags = o.value(QStringLiteral("flags")).toInt();
        if (KTextEditor::MovingRange *r = ourKeys.value(key(e.startLine, e.endLine))) {
            currentFlags.insert(r, e.flags);
            continue; // le nostre regioni le riscriviamo sotto
        }
        entries.push_back(e);
    }

    bool changed = false;
    bool persistNeeded = op == Op::Remove;
    int foldedStartLine = -1;

    for (auto it = st.ranges.begin(); it != st.ranges.end();) {
        KTextEditor::MovingRange *r = it->get();
        const bool present = currentFlags.contains(r);
        const bool folded = present && (currentFlags.value(r) & FlagFolded);
        bool wantFolded = folded;
        if (forced && forced->contains(r)) {
            wantFolded = forced->value(r);
        }

        if (r == target) {
            if (op == Op::Remove) {
                st.folded.remove(r);
                it = st.ranges.erase(it);
                changed = true;
                continue;
            }
            if (op == Op::Fold) {
                wantFolded = true;
            } else if (op == Op::Toggle) {
                wantFolded = !folded;
            }
        }

        if (!present || wantFolded != folded) {
            changed = true;
        }
        if (st.folded.value(r, !wantFolded) != wantFolded) {
            st.folded.insert(r, wantFolded);
            persistNeeded = true;
        }
        if (wantFolded && !folded) {
            foldedStartLine = startLineOf(r);
        }

        FoldEntry e;
        e.startLine = startLineOf(r);
        e.endLine = endLineOf(r);
        // come le regioni native: la prima riga resta visibile, si nasconde dal suo fine riga in poi
        e.startColumn = doc->lineLength(e.startLine);
        e.endColumn = doc->lineLength(e.endLine);
        e.flags = wantFolded ? FlagFolded : FlagPersistent;
        e.ours = true;
        entries.push_back(e);
        ++it;
    }

    if (!changed) {
        if (persistNeeded) {
            persist(st);
        }
        return;
    }

    // ordine: prima le regioni esterne, poi quelle annidate
    std::sort(entries.begin(), entries.end(), [](const FoldEntry &a, const FoldEntry &b) {
        if (a.startLine != b.startLine) {
            return a.startLine < b.startLine;
        }
        return a.endLine > b.endLine;
    });

    QJsonArray out;
    for (const FoldEntry &e : entries) {
        QJsonObject o;
        o.insert(QStringLiteral("startLine"), e.startLine);
        o.insert(QStringLiteral("startColumn"), e.startColumn);
        o.insert(QStringLiteral("endLine"), e.endLine);
        o.insert(QStringLiteral("endColumn"), e.endColumn);
        o.insert(QStringLiteral("flags"), e.flags);
        out.append(o);
    }

    QJsonObject newRoot;
    newRoot.insert(QStringLiteral("ranges"), out);
    // il checksum deve coincidere con quello del buffer, altrimenti Kate ignora lo stato
    const QString checksum = root.contains(QStringLiteral("checksum")) ? root.value(QStringLiteral("checksum")).toString()
                                                                        : QString::fromLocal8Bit(doc->checksum().toHex());
    newRoot.insert(QStringLiteral("checksum"), checksum);

    group.writeEntry("TextFolding", QJsonDocument(newRoot).toJson(QJsonDocument::Compact));

    // se contraiamo, il cursore va sulla prima riga (che resta visibile)
    if (foldedStartLine >= 0) {
        const KTextEditor::Cursor cur = view->cursorPosition();
        bool cursorHidden = false;
        for (const auto &r : st.ranges) {
            if (startLineOf(r.get()) == foldedStartLine && cur.line() > foldedStartLine && cur.line() <= endLineOf(r.get())) {
                cursorHidden = true;
            }
        }
        if (cursorHidden) {
            group.writeEntry("CursorLine", foldedStartLine);
            group.writeEntry("CursorColumn", 0);
        }
    }

    view->readSessionConfig(group);

    // Verifica: le regioni che Kate ha rifiutato (es. sovrapposizione parziale con
    // una regione di sintassi contratta) vengono rimosse, così non si entra in un ciclo.
    KConfig check(QString(), KConfig::SimpleConfig);
    KConfigGroup checkGroup(&check, QStringLiteral("Check"));
    view->writeSessionConfig(checkGroup);
    const QJsonArray after = QJsonDocument::fromJson(checkGroup.readEntry("TextFolding", QByteArray())).object().value(QStringLiteral("ranges")).toArray();
    QSet<qint64> present;
    for (const QJsonValue &v : after) {
        const QJsonObject o = v.toObject();
        present.insert(key(o.value(QStringLiteral("startLine")).toInt(), o.value(QStringLiteral("endLine")).toInt()));
    }
    bool dropped = false;
    st.ranges.erase(std::remove_if(st.ranges.begin(),
                                   st.ranges.end(),
                                   [&](const std::unique_ptr<KTextEditor::MovingRange> &r) {
                                       const bool missing = !present.contains(key(startLineOf(r.get()), endLineOf(r.get())));
                                       dropped |= missing;
                                       if (missing) {
                                           st.folded.remove(r.get());
                                       }
                                       return missing;
                                   }),
                    st.ranges.end());
    if (dropped && !forced) {
        showMessage(view, QStringLiteral("Impossibile creare il folding: si sovrappone parzialmente a un'altra regione."));
    }

    persist(st);
}

// ---------------------------------------------------------------------------
// Salvataggio / ripristino tra un avvio e l'altro di Kate
// ---------------------------------------------------------------------------

void CustomFoldingPluginView::clearRanges(CustomFoldViewState &st)
{
    st.folded.clear();
    st.ranges.clear();
}

void CustomFoldingPluginView::persist(CustomFoldViewState &st)
{
    // prima del ripristino non scriviamo: cancelleremmo i range salvati
    if (!st.restored || !st.doc || st.url.isEmpty()) {
        return;
    }
    KTextEditor::Document *doc = st.doc;

    // Lo stesso documento può essere aperto in più viste (vista divisa):
    // salviamo l'unione dei range di tutte le viste, prima quelli della vista corrente.
    QList<CustomFoldViewState *> states{&st};
    for (CustomFoldViewState *other : std::as_const(m_states)) {
        if (other != &st && other->doc == st.doc && other->url == st.url) {
            states.append(other);
        }
    }

    QJsonArray ranges;
    QSet<QPair<int, int>> seen;
    for (CustomFoldViewState *s : std::as_const(states)) {
        for (const auto &r : s->ranges) {
            if (!isUsable(r.get())) {
                continue;
            }
            const int start = startLineOf(r.get());
            const int end = endLineOf(r.get());
            if (seen.contains({start, end})) {
                continue;
            }
            seen.insert({start, end});
            QJsonObject o;
            o.insert(QStringLiteral("start"), start);
            o.insert(QStringLiteral("end"), end);
            o.insert(QStringLiteral("folded"), s->folded.value(r.get(), false));
            o.insert(QStringLiteral("startHash"), lineHash(doc, start));
            o.insert(QStringLiteral("endHash"), lineHash(doc, end));
            ranges.append(o);
        }
    }

    QJsonObject root = loadStore();
    QJsonObject files = root.value(QStringLiteral("files")).toObject();
    const QString key = st.url.toString();

    if (ranges.isEmpty()) {
        if (!files.contains(key)) {
            return;
        }
        files.remove(key);
    } else {
        QJsonObject entry;
        entry.insert(QStringLiteral("time"), QDateTime::currentMSecsSinceEpoch());
        entry.insert(QStringLiteral("ranges"), ranges);
        if (files.value(key).toObject().value(QStringLiteral("ranges")).toArray() == ranges) {
            return; // niente di nuovo
        }
        files.insert(key, entry);
    }

    // teniamo solo i file usati più di recente
    while (files.size() > MaxStoredFiles) {
        QString oldest;
        qint64 oldestTime = std::numeric_limits<qint64>::max();
        for (auto it = files.begin(); it != files.end(); ++it) {
            const qint64 t = it.value().toObject().value(QStringLiteral("time")).toInteger();
            if (t < oldestTime) {
                oldestTime = t;
                oldest = it.key();
            }
        }
        files.remove(oldest);
    }

    root.insert(QStringLiteral("files"), files);
    writeStore(root);
}

void CustomFoldingPluginView::restore(CustomFoldViewState &st)
{
    if (!st.view || !st.doc) {
        return;
    }
    KTextEditor::Document *doc = st.doc;
    st.restored = true;
    st.url = doc->url();
    if (st.url.isEmpty()) {
        return;
    }

    const QJsonArray saved =
        loadStore().value(QStringLiteral("files")).toObject().value(st.url.toString()).toObject().value(QStringLiteral("ranges")).toArray();

    QHash<KTextEditor::MovingRange *, bool> forced;
    for (const QJsonValue &v : saved) {
        const QJsonObject o = v.toObject();
        const int s = o.value(QStringLiteral("start")).toInt(-1);
        const int e = o.value(QStringLiteral("end")).toInt(-1);
        if (s < 0 || e <= s || e >= doc->lines()) {
            continue;
        }
        // il testo deve essere lo stesso di quando è stato salvato
        if (lineHash(doc, s) != o.value(QStringLiteral("startHash")).toString() || lineHash(doc, e) != o.value(QStringLiteral("endHash")).toString()) {
            continue;
        }

        bool skip = false;
        for (const auto &r : st.ranges) {
            if (!isUsable(r.get())) {
                continue;
            }
            const int rs = startLineOf(r.get());
            const int re = endLineOf(r.get());
            const bool same = rs == s && re == e;
            const bool disjoint = e < rs || s > re;
            const bool nested = (s >= rs && e <= re) || (rs >= s && re <= e);
            if (same || (!disjoint && !nested)) {
                skip = true;
                break;
            }
        }
        if (skip) {
            continue;
        }

        KTextEditor::MovingRange *range =
            doc->newMovingRange(lineRange(doc, s, e), KTextEditor::MovingRange::DoNotExpand, KTextEditor::MovingRange::InvalidateIfEmpty);
        st.ranges.emplace_back(range);
        forced.insert(range, o.value(QStringLiteral("folded")).toBool());
    }

    if (!forced.isEmpty()) {
        sync(st, nullptr, Op::None, &forced);
    }
}

#include "customfoldingplugin.moc"
