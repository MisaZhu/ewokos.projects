#ifndef NQQ_SEARCHBAR_H
#define NQQ_SEARCHBAR_H

/*
 * The bottom search/replace strip - upstream's SearchWidget: a find field
 * with prev/next, a replace row that Ctrl+H reveals, and the case / regex /
 * whole-word toggles.  The bar only collects the inputs and emits requests;
 * MainWindow runs them against the current editor, and reports "not found"
 * back so the find field turns red the way upstream's does.
 */

#include <QWidget>

class QCheckBox;
class QLineEdit;

class SearchBar : public QWidget {
    Q_OBJECT
public:
    explicit SearchBar(QWidget *parent = nullptr);

    void activate(bool withReplace);    // show, focus, select the find text
    void setNotFound(bool notFound);

    QString searchText() const;
    QString replaceText() const;
    bool caseSensitive() const;
    bool useRegex() const;
    bool wholeWords() const;

signals:
    void searchRequested(bool backward);
    void replaceRequested();
    void replaceAllRequested();
    void closed();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    QLineEdit *findEdit_;
    QLineEdit *replaceEdit_;
    QWidget *replaceRow_;
    QCheckBox *caseBox_;
    QCheckBox *regexBox_;
    QCheckBox *wordBox_;
};

#endif // NQQ_SEARCHBAR_H
