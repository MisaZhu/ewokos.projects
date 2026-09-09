/*
 * SearchBar - see searchbar.h.
 */

#include "searchbar.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QToolButton>

SearchBar::SearchBar(QWidget *parent)
    : QWidget(parent)
{
    QToolButton *closeBtn = new QToolButton(this);
    closeBtn->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
    closeBtn->setAutoRaise(true);
    connect(closeBtn, &QToolButton::clicked, this, [this]() {
        hide();
        emit closed();
    });

    findEdit_ = new QLineEdit(this);
    findEdit_->setPlaceholderText(tr("Search"));
    connect(findEdit_, &QLineEdit::returnPressed,
            this, [this]() { emit searchRequested(false); });
    connect(findEdit_, &QLineEdit::textChanged,
            this, [this](const QString &) { setNotFound(false); });

    QPushButton *prevBtn = new QPushButton(tr("Prev"), this);
    connect(prevBtn, &QPushButton::clicked,
            this, [this]() { emit searchRequested(true); });

    QPushButton *nextBtn = new QPushButton(tr("Next"), this);
    nextBtn->setDefault(false);
    connect(nextBtn, &QPushButton::clicked,
            this, [this]() { emit searchRequested(false); });

    caseBox_ = new QCheckBox(tr("Match case"), this);
    regexBox_ = new QCheckBox(tr("Regex"), this);
    wordBox_ = new QCheckBox(tr("Whole word"), this);

    replaceEdit_ = new QLineEdit(this);
    replaceEdit_->setPlaceholderText(tr("Replace with"));
    connect(replaceEdit_, &QLineEdit::returnPressed,
            this, [this]() { emit replaceRequested(); });

    QPushButton *replaceBtn = new QPushButton(tr("Replace"), this);
    connect(replaceBtn, &QPushButton::clicked,
            this, [this]() { emit replaceRequested(); });

    QPushButton *replaceAllBtn = new QPushButton(tr("Replace all"), this);
    connect(replaceAllBtn, &QPushButton::clicked,
            this, [this]() { emit replaceAllRequested(); });

    // Row 2 as one widget so Ctrl+F hides the whole replace strip at once.
    replaceRow_ = new QWidget(this);
    QHBoxLayout *rep = new QHBoxLayout(replaceRow_);
    rep->setContentsMargins(0, 0, 0, 0);
    rep->addWidget(new QLabel(tr("Replace:"), replaceRow_));
    rep->addWidget(replaceEdit_, 1);
    rep->addWidget(replaceBtn);
    rep->addWidget(replaceAllBtn);
    rep->addStretch(0);

    QGridLayout *grid = new QGridLayout(this);
    grid->setContentsMargins(4, 2, 4, 2);
    grid->setVerticalSpacing(2);
    grid->addWidget(closeBtn, 0, 0);
    grid->addWidget(new QLabel(tr("Find:"), this), 0, 1);
    grid->addWidget(findEdit_, 0, 2);
    grid->addWidget(prevBtn, 0, 3);
    grid->addWidget(nextBtn, 0, 4);
    grid->addWidget(caseBox_, 0, 5);
    grid->addWidget(regexBox_, 0, 6);
    grid->addWidget(wordBox_, 0, 7);
    grid->addWidget(replaceRow_, 1, 1, 1, 7);
    grid->setColumnStretch(2, 1);
}

void SearchBar::activate(bool withReplace)
{
    replaceRow_->setVisible(withReplace);
    show();
    findEdit_->setFocus();
    findEdit_->selectAll();
}

void SearchBar::setNotFound(bool notFound)
{
    // Upstream turns the find field red on a failed search.
    findEdit_->setStyleSheet(
        notFound ? QStringLiteral("background-color: #ff7f7f;") : QString());
}

QString SearchBar::searchText() const { return findEdit_->text(); }
QString SearchBar::replaceText() const { return replaceEdit_->text(); }
bool SearchBar::caseSensitive() const { return caseBox_->isChecked(); }
bool SearchBar::useRegex() const { return regexBox_->isChecked(); }
bool SearchBar::wholeWords() const { return wordBox_->isChecked(); }

void SearchBar::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        hide();
        emit closed();
        return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        (event->modifiers() & Qt::ShiftModifier)) {
        emit searchRequested(true);
        return;
    }
    QWidget::keyPressEvent(event);
}
