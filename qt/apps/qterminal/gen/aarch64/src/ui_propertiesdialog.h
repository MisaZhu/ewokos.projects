/********************************************************************************
** Form generated from reading UI file 'propertiesdialog.ui'
**
** Created by: Qt User Interface Compiler version 5.15.19
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PROPERTIESDIALOG_H
#define UI_PROPERTIESDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QRadioButton>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_PropertiesDialog
{
public:
    QGridLayout *gridLayout_4;
    QListWidget *listWidget;
    QStackedWidget *stackedWidget;
    QWidget *appearancePage;
    QVBoxLayout *verticalLayout_4;
    QScrollArea *scrollArea;
    QWidget *scrollAreaWidgetContents;
    QGridLayout *gridLayout_3;
    QWidget *fontWidget;
    QHBoxLayout *horizontalLayout_2;
    QLabel *fontLabel;
    QSpacerItem *fontSpacer;
    QLabel *fontSampleLabel;
    QPushButton *changeFontButton;
    QLabel *label_5;
    QComboBox *colorSchemaCombo;
    QLabel *label_6;
    QComboBox *styleComboBox;
    QLabel *label_7;
    QComboBox *scrollBarPos_comboBox;
    QLabel *label_8;
    QComboBox *tabsPos_comboBox;
    QLabel *label_12;
    QComboBox *keybCursorShape_comboBox;
    QCheckBox *boldIntenseCheckBox;
    QCheckBox *menuAccelCheckBox;
    QCheckBox *showMenuCheckBox;
    QCheckBox *borderlessCheckBox;
    QCheckBox *hideTabBarCheckBox;
    QCheckBox *fixedTabWidthCheckBox;
    QSpinBox *fixedTabWidthSpinBox;
    QCheckBox *highlightCurrentCheckBox;
    QCheckBox *closeTabButtonCheckBox;
    QCheckBox *changeWindowTitleCheckBox;
    QCheckBox *changeWindowIconCheckBox;
    QCheckBox *showTerminalSizeHintCheckBox;
    QCheckBox *enabledBidiSupportCheckBox;
    QCheckBox *useFontBoxDrawingCharsCheckBox;
    QLabel *label_4;
    QSpinBox *appTransparencyBox;
    QLabel *label;
    QSpinBox *termTransparencyBox;
    QLabel *label_13;
    QHBoxLayout *horizontalLayout_3;
    QLineEdit *backgroundImageLineEdit;
    QPushButton *chooseBackgroundImageButton;
    QLabel *label_16;
    QComboBox *backgroundModecomboBox;
    QLabel *label_9;
    QComboBox *terminalPresetComboBox;
    QLabel *label_15;
    QSpinBox *terminalMarginSpinBox;
    QSpacerItem *verticalSpacer_3;
    QWidget *behaviorPage;
    QVBoxLayout *verticalLayout_5;
    QScrollArea *scrollArea1;
    QWidget *scrollAreaWidgetContents1;
    QGridLayout *gridLayout_5;
    QSpacerItem *verticalSpacer_2;
    QGroupBox *groupBox_2;
    QGridLayout *gridLayout_2;
    QRadioButton *historyLimited;
    QRadioButton *historyUnlimited;
    QSpinBox *historyLimitedTo;
    QLabel *label_3;
    QComboBox *motionAfterPasting_comboBox;
    QLabel *label_14;
    QCheckBox *disableBracketedPasteModeCheckBox;
    QCheckBox *confirmMultilinePasteCheckBox;
    QCheckBox *trimPastedTrailingNewlinesCheckBox;
    QCheckBox *closeTabOnMiddleClickCheckBox;
    QCheckBox *askOnExitCheckBox;
    QCheckBox *savePosOnExitCheckBox;
    QCheckBox *saveSizeOnExitCheckBox;
    QHBoxLayout *horizontalLayout_4;
    QLabel *fixedSizeLabel;
    QSpinBox *fixedWithSpinBox;
    QLabel *xLabel;
    QSpinBox *fixedHeightSpinBox;
    QPushButton *getCurrentSizeButton;
    QSpacerItem *horizontalSpacer;
    QCheckBox *useCwdCheckBox;
    QCheckBox *openNewTabRightToActiveTabCheckBox;
    QCheckBox *audibleBellCheckBox;
    QComboBox *termComboBox;
    QLineEdit *handleHistoryLineEdit;
    QLabel *label_17;
    QGroupBox *groupBox_4;
    QGridLayout *gridLayout_6;
    QComboBox *emulationComboBox;
    QLabel *label_2;
    QWidget *shortcutsPage;
    QVBoxLayout *verticalLayout;
    QTableWidget *shortcutsWidget;
    QWidget *dropdownPage;
    QVBoxLayout *verticalLayout_3;
    QCheckBox *dropShowOnStartCheckBox;
    QCheckBox *dropKeepOpenCheckBox;
    QGroupBox *groupBox_3;
    QVBoxLayout *verticalLayout_2;
    QFormLayout *formLayout;
    QLabel *dropHeightLabel;
    QSpinBox *dropHeightSpinBox;
    QLabel *dropWidthLabel;
    QSpinBox *dropWidthSpinBox;
    QFormLayout *dropShortCutFormLayout;
    QLabel *dropShortCutLabel;
    QSpacerItem *verticalSpacer_5;
    QWidget *bookmarksPage;
    QGridLayout *gridLayout_9;
    QCheckBox *useBookmarksCheckBox;
    QHBoxLayout *FindBookmarkLayout;
    QLabel *label_10;
    QLineEdit *bookmarksLineEdit;
    QPushButton *bookmarksButton;
    QLabel *label_11;
    QGroupBox *groupBox_5;
    QGridLayout *gridLayout_10;
    QPlainTextEdit *bookmarkPlainEdit;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *PropertiesDialog)
    {
        if (PropertiesDialog->objectName().isEmpty())
            PropertiesDialog->setObjectName(QString::fromUtf8("PropertiesDialog"));
        PropertiesDialog->resize(700, 700);
        gridLayout_4 = new QGridLayout(PropertiesDialog);
        gridLayout_4->setObjectName(QString::fromUtf8("gridLayout_4"));
        listWidget = new QListWidget(PropertiesDialog);
        QIcon icon;
        QString iconThemeName = QString::fromUtf8("preferences-desktop-theme");
        if (QIcon::hasThemeIcon(iconThemeName)) {
            icon = QIcon::fromTheme(iconThemeName);
        } else {
            icon.addFile(QString::fromUtf8("."), QSize(), QIcon::Normal, QIcon::Off);
        }
        QListWidgetItem *__qlistwidgetitem = new QListWidgetItem(listWidget);
        __qlistwidgetitem->setIcon(icon);
        QIcon icon1;
        iconThemeName = QString::fromUtf8("preferences-system");
        if (QIcon::hasThemeIcon(iconThemeName)) {
            icon1 = QIcon::fromTheme(iconThemeName);
        } else {
            icon1.addFile(QString::fromUtf8("."), QSize(), QIcon::Normal, QIcon::Off);
        }
        QListWidgetItem *__qlistwidgetitem1 = new QListWidgetItem(listWidget);
        __qlistwidgetitem1->setIcon(icon1);
        QIcon icon2;
        iconThemeName = QString::fromUtf8("preferences-desktop-keyboard");
        if (QIcon::hasThemeIcon(iconThemeName)) {
            icon2 = QIcon::fromTheme(iconThemeName);
        } else {
            icon2.addFile(QString::fromUtf8("."), QSize(), QIcon::Normal, QIcon::Off);
        }
        QListWidgetItem *__qlistwidgetitem2 = new QListWidgetItem(listWidget);
        __qlistwidgetitem2->setIcon(icon2);
        QIcon icon3;
        iconThemeName = QString::fromUtf8("utilities-terminal");
        if (QIcon::hasThemeIcon(iconThemeName)) {
            icon3 = QIcon::fromTheme(iconThemeName);
        } else {
            icon3.addFile(QString::fromUtf8("."), QSize(), QIcon::Normal, QIcon::Off);
        }
        QListWidgetItem *__qlistwidgetitem3 = new QListWidgetItem(listWidget);
        __qlistwidgetitem3->setIcon(icon3);
        QIcon icon4;
        iconThemeName = QString::fromUtf8("bookmark-new");
        if (QIcon::hasThemeIcon(iconThemeName)) {
            icon4 = QIcon::fromTheme(iconThemeName);
        } else {
            icon4.addFile(QString::fromUtf8("."), QSize(), QIcon::Normal, QIcon::Off);
        }
        QListWidgetItem *__qlistwidgetitem4 = new QListWidgetItem(listWidget);
        __qlistwidgetitem4->setIcon(icon4);
        listWidget->setObjectName(QString::fromUtf8("listWidget"));

        gridLayout_4->addWidget(listWidget, 0, 0, 1, 1);

        stackedWidget = new QStackedWidget(PropertiesDialog);
        stackedWidget->setObjectName(QString::fromUtf8("stackedWidget"));
        stackedWidget->setFrameShape(QFrame::StyledPanel);
        appearancePage = new QWidget();
        appearancePage->setObjectName(QString::fromUtf8("appearancePage"));
        verticalLayout_4 = new QVBoxLayout(appearancePage);
        verticalLayout_4->setSpacing(0);
        verticalLayout_4->setObjectName(QString::fromUtf8("verticalLayout_4"));
        verticalLayout_4->setContentsMargins(0, 0, 0, 0);
        scrollArea = new QScrollArea(appearancePage);
        scrollArea->setObjectName(QString::fromUtf8("scrollArea"));
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setWidgetResizable(true);
        scrollAreaWidgetContents = new QWidget();
        scrollAreaWidgetContents->setObjectName(QString::fromUtf8("scrollAreaWidgetContents"));
        scrollAreaWidgetContents->setGeometry(QRect(0, 0, 440, 925));
        gridLayout_3 = new QGridLayout(scrollAreaWidgetContents);
        gridLayout_3->setObjectName(QString::fromUtf8("gridLayout_3"));
        fontWidget = new QWidget(scrollAreaWidgetContents);
        fontWidget->setObjectName(QString::fromUtf8("fontWidget"));
        horizontalLayout_2 = new QHBoxLayout(fontWidget);
        horizontalLayout_2->setObjectName(QString::fromUtf8("horizontalLayout_2"));
        horizontalLayout_2->setContentsMargins(0, 0, 0, 0);
        fontLabel = new QLabel(fontWidget);
        fontLabel->setObjectName(QString::fromUtf8("fontLabel"));

        horizontalLayout_2->addWidget(fontLabel);

        fontSpacer = new QSpacerItem(40, 20, QSizePolicy::Expanding, QSizePolicy::Minimum);

        horizontalLayout_2->addItem(fontSpacer);

        fontSampleLabel = new QLabel(fontWidget);
        fontSampleLabel->setObjectName(QString::fromUtf8("fontSampleLabel"));
        fontSampleLabel->setFrameShape(QFrame::StyledPanel);
        fontSampleLabel->setWordWrap(false);

        horizontalLayout_2->addWidget(fontSampleLabel);

        changeFontButton = new QPushButton(fontWidget);
        changeFontButton->setObjectName(QString::fromUtf8("changeFontButton"));

        horizontalLayout_2->addWidget(changeFontButton);


        gridLayout_3->addWidget(fontWidget, 0, 0, 2, 2);

        label_5 = new QLabel(scrollAreaWidgetContents);
        label_5->setObjectName(QString::fromUtf8("label_5"));

        gridLayout_3->addWidget(label_5, 2, 0, 1, 1);

        colorSchemaCombo = new QComboBox(scrollAreaWidgetContents);
        colorSchemaCombo->setObjectName(QString::fromUtf8("colorSchemaCombo"));

        gridLayout_3->addWidget(colorSchemaCombo, 2, 1, 1, 1);

        label_6 = new QLabel(scrollAreaWidgetContents);
        label_6->setObjectName(QString::fromUtf8("label_6"));

        gridLayout_3->addWidget(label_6, 3, 0, 1, 1);

        styleComboBox = new QComboBox(scrollAreaWidgetContents);
        styleComboBox->setObjectName(QString::fromUtf8("styleComboBox"));

        gridLayout_3->addWidget(styleComboBox, 3, 1, 1, 1);

        label_7 = new QLabel(scrollAreaWidgetContents);
        label_7->setObjectName(QString::fromUtf8("label_7"));

        gridLayout_3->addWidget(label_7, 4, 0, 1, 1);

        scrollBarPos_comboBox = new QComboBox(scrollAreaWidgetContents);
        scrollBarPos_comboBox->setObjectName(QString::fromUtf8("scrollBarPos_comboBox"));

        gridLayout_3->addWidget(scrollBarPos_comboBox, 4, 1, 1, 1);

        label_8 = new QLabel(scrollAreaWidgetContents);
        label_8->setObjectName(QString::fromUtf8("label_8"));

        gridLayout_3->addWidget(label_8, 5, 0, 1, 1);

        tabsPos_comboBox = new QComboBox(scrollAreaWidgetContents);
        tabsPos_comboBox->setObjectName(QString::fromUtf8("tabsPos_comboBox"));

        gridLayout_3->addWidget(tabsPos_comboBox, 5, 1, 1, 1);

        label_12 = new QLabel(scrollAreaWidgetContents);
        label_12->setObjectName(QString::fromUtf8("label_12"));

        gridLayout_3->addWidget(label_12, 6, 0, 1, 1);

        keybCursorShape_comboBox = new QComboBox(scrollAreaWidgetContents);
        keybCursorShape_comboBox->setObjectName(QString::fromUtf8("keybCursorShape_comboBox"));

        gridLayout_3->addWidget(keybCursorShape_comboBox, 6, 1, 1, 1);

        boldIntenseCheckBox = new QCheckBox(scrollAreaWidgetContents);
        boldIntenseCheckBox->setObjectName(QString::fromUtf8("boldIntenseCheckBox"));

        gridLayout_3->addWidget(boldIntenseCheckBox, 7, 0, 1, 2);

        menuAccelCheckBox = new QCheckBox(scrollAreaWidgetContents);
        menuAccelCheckBox->setObjectName(QString::fromUtf8("menuAccelCheckBox"));

        gridLayout_3->addWidget(menuAccelCheckBox, 8, 0, 1, 2);

        showMenuCheckBox = new QCheckBox(scrollAreaWidgetContents);
        showMenuCheckBox->setObjectName(QString::fromUtf8("showMenuCheckBox"));

        gridLayout_3->addWidget(showMenuCheckBox, 9, 0, 1, 2);

        borderlessCheckBox = new QCheckBox(scrollAreaWidgetContents);
        borderlessCheckBox->setObjectName(QString::fromUtf8("borderlessCheckBox"));

        gridLayout_3->addWidget(borderlessCheckBox, 10, 0, 1, 2);

        hideTabBarCheckBox = new QCheckBox(scrollAreaWidgetContents);
        hideTabBarCheckBox->setObjectName(QString::fromUtf8("hideTabBarCheckBox"));

        gridLayout_3->addWidget(hideTabBarCheckBox, 11, 0, 1, 2);

        fixedTabWidthCheckBox = new QCheckBox(scrollAreaWidgetContents);
        fixedTabWidthCheckBox->setObjectName(QString::fromUtf8("fixedTabWidthCheckBox"));

        gridLayout_3->addWidget(fixedTabWidthCheckBox, 12, 0, 1, 1);

        fixedTabWidthSpinBox = new QSpinBox(scrollAreaWidgetContents);
        fixedTabWidthSpinBox->setObjectName(QString::fromUtf8("fixedTabWidthSpinBox"));
        fixedTabWidthSpinBox->setEnabled(false);
        fixedTabWidthSpinBox->setMaximum(1000);

        gridLayout_3->addWidget(fixedTabWidthSpinBox, 12, 1, 1, 1);

        highlightCurrentCheckBox = new QCheckBox(scrollAreaWidgetContents);
        highlightCurrentCheckBox->setObjectName(QString::fromUtf8("highlightCurrentCheckBox"));

        gridLayout_3->addWidget(highlightCurrentCheckBox, 13, 0, 1, 2);

        closeTabButtonCheckBox = new QCheckBox(scrollAreaWidgetContents);
        closeTabButtonCheckBox->setObjectName(QString::fromUtf8("closeTabButtonCheckBox"));
        closeTabButtonCheckBox->setChecked(false);

        gridLayout_3->addWidget(closeTabButtonCheckBox, 14, 0, 1, 2);

        changeWindowTitleCheckBox = new QCheckBox(scrollAreaWidgetContents);
        changeWindowTitleCheckBox->setObjectName(QString::fromUtf8("changeWindowTitleCheckBox"));

        gridLayout_3->addWidget(changeWindowTitleCheckBox, 15, 0, 1, 2);

        changeWindowIconCheckBox = new QCheckBox(scrollAreaWidgetContents);
        changeWindowIconCheckBox->setObjectName(QString::fromUtf8("changeWindowIconCheckBox"));

        gridLayout_3->addWidget(changeWindowIconCheckBox, 16, 0, 1, 2);

        showTerminalSizeHintCheckBox = new QCheckBox(scrollAreaWidgetContents);
        showTerminalSizeHintCheckBox->setObjectName(QString::fromUtf8("showTerminalSizeHintCheckBox"));

        gridLayout_3->addWidget(showTerminalSizeHintCheckBox, 17, 0, 1, 2);

        enabledBidiSupportCheckBox = new QCheckBox(scrollAreaWidgetContents);
        enabledBidiSupportCheckBox->setObjectName(QString::fromUtf8("enabledBidiSupportCheckBox"));

        gridLayout_3->addWidget(enabledBidiSupportCheckBox, 18, 0, 1, 2);

        useFontBoxDrawingCharsCheckBox = new QCheckBox(scrollAreaWidgetContents);
        useFontBoxDrawingCharsCheckBox->setObjectName(QString::fromUtf8("useFontBoxDrawingCharsCheckBox"));

        gridLayout_3->addWidget(useFontBoxDrawingCharsCheckBox, 19, 0, 1, 2);

        label_4 = new QLabel(scrollAreaWidgetContents);
        label_4->setObjectName(QString::fromUtf8("label_4"));

        gridLayout_3->addWidget(label_4, 20, 0, 1, 1);

        appTransparencyBox = new QSpinBox(scrollAreaWidgetContents);
        appTransparencyBox->setObjectName(QString::fromUtf8("appTransparencyBox"));
        appTransparencyBox->setMinimum(0);
        appTransparencyBox->setMaximum(99);
        appTransparencyBox->setValue(0);

        gridLayout_3->addWidget(appTransparencyBox, 20, 1, 1, 1);

        label = new QLabel(scrollAreaWidgetContents);
        label->setObjectName(QString::fromUtf8("label"));

        gridLayout_3->addWidget(label, 21, 0, 1, 1);

        termTransparencyBox = new QSpinBox(scrollAreaWidgetContents);
        termTransparencyBox->setObjectName(QString::fromUtf8("termTransparencyBox"));
        termTransparencyBox->setMinimum(0);
        termTransparencyBox->setMaximum(100);
        termTransparencyBox->setValue(0);

        gridLayout_3->addWidget(termTransparencyBox, 21, 1, 1, 1);

        label_13 = new QLabel(scrollAreaWidgetContents);
        label_13->setObjectName(QString::fromUtf8("label_13"));

        gridLayout_3->addWidget(label_13, 22, 0, 1, 1);

        horizontalLayout_3 = new QHBoxLayout();
        horizontalLayout_3->setObjectName(QString::fromUtf8("horizontalLayout_3"));
        backgroundImageLineEdit = new QLineEdit(scrollAreaWidgetContents);
        backgroundImageLineEdit->setObjectName(QString::fromUtf8("backgroundImageLineEdit"));

        horizontalLayout_3->addWidget(backgroundImageLineEdit);

        chooseBackgroundImageButton = new QPushButton(scrollAreaWidgetContents);
        chooseBackgroundImageButton->setObjectName(QString::fromUtf8("chooseBackgroundImageButton"));

        horizontalLayout_3->addWidget(chooseBackgroundImageButton);


        gridLayout_3->addLayout(horizontalLayout_3, 22, 1, 1, 1);

        label_16 = new QLabel(scrollAreaWidgetContents);
        label_16->setObjectName(QString::fromUtf8("label_16"));

        gridLayout_3->addWidget(label_16, 23, 0, 1, 1);

        backgroundModecomboBox = new QComboBox(scrollAreaWidgetContents);
        backgroundModecomboBox->addItem(QString());
        backgroundModecomboBox->addItem(QString());
        backgroundModecomboBox->addItem(QString());
        backgroundModecomboBox->addItem(QString());
        backgroundModecomboBox->addItem(QString());
        backgroundModecomboBox->setObjectName(QString::fromUtf8("backgroundModecomboBox"));

        gridLayout_3->addWidget(backgroundModecomboBox, 23, 1, 1, 1);

        label_9 = new QLabel(scrollAreaWidgetContents);
        label_9->setObjectName(QString::fromUtf8("label_9"));

        gridLayout_3->addWidget(label_9, 24, 0, 1, 1);

        terminalPresetComboBox = new QComboBox(scrollAreaWidgetContents);
        terminalPresetComboBox->addItem(QString());
        terminalPresetComboBox->addItem(QString());
        terminalPresetComboBox->addItem(QString());
        terminalPresetComboBox->addItem(QString());
        terminalPresetComboBox->setObjectName(QString::fromUtf8("terminalPresetComboBox"));

        gridLayout_3->addWidget(terminalPresetComboBox, 24, 1, 1, 1);

        label_15 = new QLabel(scrollAreaWidgetContents);
        label_15->setObjectName(QString::fromUtf8("label_15"));

        gridLayout_3->addWidget(label_15, 25, 0, 1, 1);

        terminalMarginSpinBox = new QSpinBox(scrollAreaWidgetContents);
        terminalMarginSpinBox->setObjectName(QString::fromUtf8("terminalMarginSpinBox"));

        gridLayout_3->addWidget(terminalMarginSpinBox, 25, 1, 1, 1);

        verticalSpacer_3 = new QSpacerItem(20, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout_3->addItem(verticalSpacer_3, 26, 0, 1, 2);

        scrollArea->setWidget(scrollAreaWidgetContents);

        verticalLayout_4->addWidget(scrollArea);

        stackedWidget->addWidget(appearancePage);
        behaviorPage = new QWidget();
        behaviorPage->setObjectName(QString::fromUtf8("behaviorPage"));
        verticalLayout_5 = new QVBoxLayout(behaviorPage);
        verticalLayout_5->setSpacing(0);
        verticalLayout_5->setObjectName(QString::fromUtf8("verticalLayout_5"));
        verticalLayout_5->setContentsMargins(0, 0, 0, 0);
        scrollArea1 = new QScrollArea(behaviorPage);
        scrollArea1->setObjectName(QString::fromUtf8("scrollArea1"));
        scrollArea1->setFrameShape(QFrame::NoFrame);
        scrollArea1->setWidgetResizable(true);
        scrollAreaWidgetContents1 = new QWidget();
        scrollAreaWidgetContents1->setObjectName(QString::fromUtf8("scrollAreaWidgetContents1"));
        scrollAreaWidgetContents1->setGeometry(QRect(0, 0, 490, 872));
        gridLayout_5 = new QGridLayout(scrollAreaWidgetContents1);
        gridLayout_5->setObjectName(QString::fromUtf8("gridLayout_5"));
        verticalSpacer_2 = new QSpacerItem(20, 57, QSizePolicy::Minimum, QSizePolicy::Expanding);

        gridLayout_5->addItem(verticalSpacer_2, 2, 0, 1, 1);

        groupBox_2 = new QGroupBox(scrollAreaWidgetContents1);
        groupBox_2->setObjectName(QString::fromUtf8("groupBox_2"));
        gridLayout_2 = new QGridLayout(groupBox_2);
        gridLayout_2->setObjectName(QString::fromUtf8("gridLayout_2"));
        historyLimited = new QRadioButton(groupBox_2);
        historyLimited->setObjectName(QString::fromUtf8("historyLimited"));

        gridLayout_2->addWidget(historyLimited, 0, 0, 1, 1);

        historyUnlimited = new QRadioButton(groupBox_2);
        historyUnlimited->setObjectName(QString::fromUtf8("historyUnlimited"));

        gridLayout_2->addWidget(historyUnlimited, 1, 0, 1, 2);

        historyLimitedTo = new QSpinBox(groupBox_2);
        historyLimitedTo->setObjectName(QString::fromUtf8("historyLimitedTo"));
        historyLimitedTo->setMinimum(100);
        historyLimitedTo->setMaximum(1000000);
        historyLimitedTo->setValue(1000);

        gridLayout_2->addWidget(historyLimitedTo, 0, 1, 1, 1);

        label_3 = new QLabel(groupBox_2);
        label_3->setObjectName(QString::fromUtf8("label_3"));

        gridLayout_2->addWidget(label_3, 2, 0, 1, 1);

        motionAfterPasting_comboBox = new QComboBox(groupBox_2);
        motionAfterPasting_comboBox->setObjectName(QString::fromUtf8("motionAfterPasting_comboBox"));

        gridLayout_2->addWidget(motionAfterPasting_comboBox, 2, 1, 1, 1);

        label_14 = new QLabel(groupBox_2);
        label_14->setObjectName(QString::fromUtf8("label_14"));

        gridLayout_2->addWidget(label_14, 14, 0, 1, 1);

        disableBracketedPasteModeCheckBox = new QCheckBox(groupBox_2);
        disableBracketedPasteModeCheckBox->setObjectName(QString::fromUtf8("disableBracketedPasteModeCheckBox"));

        gridLayout_2->addWidget(disableBracketedPasteModeCheckBox, 3, 0, 1, 2);

        confirmMultilinePasteCheckBox = new QCheckBox(groupBox_2);
        confirmMultilinePasteCheckBox->setObjectName(QString::fromUtf8("confirmMultilinePasteCheckBox"));

        gridLayout_2->addWidget(confirmMultilinePasteCheckBox, 4, 0, 1, 2);

        trimPastedTrailingNewlinesCheckBox = new QCheckBox(groupBox_2);
        trimPastedTrailingNewlinesCheckBox->setObjectName(QString::fromUtf8("trimPastedTrailingNewlinesCheckBox"));

        gridLayout_2->addWidget(trimPastedTrailingNewlinesCheckBox, 5, 0, 1, 2);

        closeTabOnMiddleClickCheckBox = new QCheckBox(groupBox_2);
        closeTabOnMiddleClickCheckBox->setObjectName(QString::fromUtf8("closeTabOnMiddleClickCheckBox"));
        closeTabOnMiddleClickCheckBox->setChecked(true);

        gridLayout_2->addWidget(closeTabOnMiddleClickCheckBox, 6, 0, 1, 2);

        askOnExitCheckBox = new QCheckBox(groupBox_2);
        askOnExitCheckBox->setObjectName(QString::fromUtf8("askOnExitCheckBox"));

        gridLayout_2->addWidget(askOnExitCheckBox, 7, 0, 1, 2);

        savePosOnExitCheckBox = new QCheckBox(groupBox_2);
        savePosOnExitCheckBox->setObjectName(QString::fromUtf8("savePosOnExitCheckBox"));

        gridLayout_2->addWidget(savePosOnExitCheckBox, 8, 0, 1, 2);

        saveSizeOnExitCheckBox = new QCheckBox(groupBox_2);
        saveSizeOnExitCheckBox->setObjectName(QString::fromUtf8("saveSizeOnExitCheckBox"));

        gridLayout_2->addWidget(saveSizeOnExitCheckBox, 9, 0, 1, 2);

        horizontalLayout_4 = new QHBoxLayout();
        horizontalLayout_4->setSpacing(5);
        horizontalLayout_4->setObjectName(QString::fromUtf8("horizontalLayout_4"));
        fixedSizeLabel = new QLabel(groupBox_2);
        fixedSizeLabel->setObjectName(QString::fromUtf8("fixedSizeLabel"));

        horizontalLayout_4->addWidget(fixedSizeLabel);

        fixedWithSpinBox = new QSpinBox(groupBox_2);
        fixedWithSpinBox->setObjectName(QString::fromUtf8("fixedWithSpinBox"));

        horizontalLayout_4->addWidget(fixedWithSpinBox);

        xLabel = new QLabel(groupBox_2);
        xLabel->setObjectName(QString::fromUtf8("xLabel"));
        xLabel->setText(QString::fromUtf8("\303\227"));

        horizontalLayout_4->addWidget(xLabel);

        fixedHeightSpinBox = new QSpinBox(groupBox_2);
        fixedHeightSpinBox->setObjectName(QString::fromUtf8("fixedHeightSpinBox"));

        horizontalLayout_4->addWidget(fixedHeightSpinBox);

        getCurrentSizeButton = new QPushButton(groupBox_2);
        getCurrentSizeButton->setObjectName(QString::fromUtf8("getCurrentSizeButton"));

        horizontalLayout_4->addWidget(getCurrentSizeButton);

        horizontalSpacer = new QSpacerItem(5, 5, QSizePolicy::MinimumExpanding, QSizePolicy::Minimum);

        horizontalLayout_4->addItem(horizontalSpacer);


        gridLayout_2->addLayout(horizontalLayout_4, 10, 0, 1, 2);

        useCwdCheckBox = new QCheckBox(groupBox_2);
        useCwdCheckBox->setObjectName(QString::fromUtf8("useCwdCheckBox"));

        gridLayout_2->addWidget(useCwdCheckBox, 11, 0, 1, 2);

        openNewTabRightToActiveTabCheckBox = new QCheckBox(groupBox_2);
        openNewTabRightToActiveTabCheckBox->setObjectName(QString::fromUtf8("openNewTabRightToActiveTabCheckBox"));

        gridLayout_2->addWidget(openNewTabRightToActiveTabCheckBox, 12, 0, 1, 2);

        audibleBellCheckBox = new QCheckBox(groupBox_2);
        audibleBellCheckBox->setObjectName(QString::fromUtf8("audibleBellCheckBox"));

        gridLayout_2->addWidget(audibleBellCheckBox, 13, 0, 1, 1);

        termComboBox = new QComboBox(groupBox_2);
        termComboBox->addItem(QString::fromUtf8("xterm"));
        termComboBox->addItem(QString::fromUtf8("xterm-256color"));
        termComboBox->setObjectName(QString::fromUtf8("termComboBox"));
        QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(termComboBox->sizePolicy().hasHeightForWidth());
        termComboBox->setSizePolicy(sizePolicy);
        termComboBox->setEditable(true);

        gridLayout_2->addWidget(termComboBox, 14, 1, 1, 1);

        handleHistoryLineEdit = new QLineEdit(groupBox_2);
        handleHistoryLineEdit->setObjectName(QString::fromUtf8("handleHistoryLineEdit"));

        gridLayout_2->addWidget(handleHistoryLineEdit, 15, 1, 1, 1);

        label_17 = new QLabel(groupBox_2);
        label_17->setObjectName(QString::fromUtf8("label_17"));

        gridLayout_2->addWidget(label_17, 15, 0, 1, 1);


        gridLayout_5->addWidget(groupBox_2, 0, 0, 1, 1);

        groupBox_4 = new QGroupBox(scrollAreaWidgetContents1);
        groupBox_4->setObjectName(QString::fromUtf8("groupBox_4"));
        gridLayout_6 = new QGridLayout(groupBox_4);
        gridLayout_6->setObjectName(QString::fromUtf8("gridLayout_6"));
        emulationComboBox = new QComboBox(groupBox_4);
        emulationComboBox->setObjectName(QString::fromUtf8("emulationComboBox"));

        gridLayout_6->addWidget(emulationComboBox, 0, 0, 1, 1);

        label_2 = new QLabel(groupBox_4);
        label_2->setObjectName(QString::fromUtf8("label_2"));
        label_2->setTextFormat(Qt::RichText);
        label_2->setWordWrap(true);

        gridLayout_6->addWidget(label_2, 1, 0, 1, 1);


        gridLayout_5->addWidget(groupBox_4, 1, 0, 1, 1);

        scrollArea1->setWidget(scrollAreaWidgetContents1);

        verticalLayout_5->addWidget(scrollArea1);

        stackedWidget->addWidget(behaviorPage);
        shortcutsPage = new QWidget();
        shortcutsPage->setObjectName(QString::fromUtf8("shortcutsPage"));
        verticalLayout = new QVBoxLayout(shortcutsPage);
        verticalLayout->setObjectName(QString::fromUtf8("verticalLayout"));
        shortcutsWidget = new QTableWidget(shortcutsPage);
        if (shortcutsWidget->columnCount() < 2)
            shortcutsWidget->setColumnCount(2);
        QTableWidgetItem *__qtablewidgetitem = new QTableWidgetItem();
        shortcutsWidget->setHorizontalHeaderItem(0, __qtablewidgetitem);
        QTableWidgetItem *__qtablewidgetitem1 = new QTableWidgetItem();
        shortcutsWidget->setHorizontalHeaderItem(1, __qtablewidgetitem1);
        shortcutsWidget->setObjectName(QString::fromUtf8("shortcutsWidget"));
        shortcutsWidget->setAlternatingRowColors(true);
        shortcutsWidget->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        shortcutsWidget->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
        shortcutsWidget->setSortingEnabled(true);
        shortcutsWidget->horizontalHeader()->setDefaultSectionSize(105);
        shortcutsWidget->horizontalHeader()->setStretchLastSection(true);
        shortcutsWidget->verticalHeader()->setVisible(false);

        verticalLayout->addWidget(shortcutsWidget);

        stackedWidget->addWidget(shortcutsPage);
        dropdownPage = new QWidget();
        dropdownPage->setObjectName(QString::fromUtf8("dropdownPage"));
        verticalLayout_3 = new QVBoxLayout(dropdownPage);
        verticalLayout_3->setObjectName(QString::fromUtf8("verticalLayout_3"));
        dropShowOnStartCheckBox = new QCheckBox(dropdownPage);
        dropShowOnStartCheckBox->setObjectName(QString::fromUtf8("dropShowOnStartCheckBox"));

        verticalLayout_3->addWidget(dropShowOnStartCheckBox);

        dropKeepOpenCheckBox = new QCheckBox(dropdownPage);
        dropKeepOpenCheckBox->setObjectName(QString::fromUtf8("dropKeepOpenCheckBox"));

        verticalLayout_3->addWidget(dropKeepOpenCheckBox);

        groupBox_3 = new QGroupBox(dropdownPage);
        groupBox_3->setObjectName(QString::fromUtf8("groupBox_3"));
        verticalLayout_2 = new QVBoxLayout(groupBox_3);
        verticalLayout_2->setObjectName(QString::fromUtf8("verticalLayout_2"));
        formLayout = new QFormLayout();
        formLayout->setObjectName(QString::fromUtf8("formLayout"));
        formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        dropHeightLabel = new QLabel(groupBox_3);
        dropHeightLabel->setObjectName(QString::fromUtf8("dropHeightLabel"));

        formLayout->setWidget(0, QFormLayout::LabelRole, dropHeightLabel);

        dropHeightSpinBox = new QSpinBox(groupBox_3);
        dropHeightSpinBox->setObjectName(QString::fromUtf8("dropHeightSpinBox"));

        formLayout->setWidget(0, QFormLayout::FieldRole, dropHeightSpinBox);

        dropWidthLabel = new QLabel(groupBox_3);
        dropWidthLabel->setObjectName(QString::fromUtf8("dropWidthLabel"));

        formLayout->setWidget(1, QFormLayout::LabelRole, dropWidthLabel);

        dropWidthSpinBox = new QSpinBox(groupBox_3);
        dropWidthSpinBox->setObjectName(QString::fromUtf8("dropWidthSpinBox"));

        formLayout->setWidget(1, QFormLayout::FieldRole, dropWidthSpinBox);


        verticalLayout_2->addLayout(formLayout);


        verticalLayout_3->addWidget(groupBox_3);

        dropShortCutFormLayout = new QFormLayout();
        dropShortCutFormLayout->setObjectName(QString::fromUtf8("dropShortCutFormLayout"));
        dropShortCutLabel = new QLabel(dropdownPage);
        dropShortCutLabel->setObjectName(QString::fromUtf8("dropShortCutLabel"));

        dropShortCutFormLayout->setWidget(0, QFormLayout::LabelRole, dropShortCutLabel);


        verticalLayout_3->addLayout(dropShortCutFormLayout);

        verticalSpacer_5 = new QSpacerItem(20, 78, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout_3->addItem(verticalSpacer_5);

        stackedWidget->addWidget(dropdownPage);
        bookmarksPage = new QWidget();
        bookmarksPage->setObjectName(QString::fromUtf8("bookmarksPage"));
        gridLayout_9 = new QGridLayout(bookmarksPage);
        gridLayout_9->setObjectName(QString::fromUtf8("gridLayout_9"));
        useBookmarksCheckBox = new QCheckBox(bookmarksPage);
        useBookmarksCheckBox->setObjectName(QString::fromUtf8("useBookmarksCheckBox"));

        gridLayout_9->addWidget(useBookmarksCheckBox, 0, 0, 1, 1);

        FindBookmarkLayout = new QHBoxLayout();
        FindBookmarkLayout->setObjectName(QString::fromUtf8("FindBookmarkLayout"));
        label_10 = new QLabel(bookmarksPage);
        label_10->setObjectName(QString::fromUtf8("label_10"));

        FindBookmarkLayout->addWidget(label_10);

        bookmarksLineEdit = new QLineEdit(bookmarksPage);
        bookmarksLineEdit->setObjectName(QString::fromUtf8("bookmarksLineEdit"));

        FindBookmarkLayout->addWidget(bookmarksLineEdit);

        bookmarksButton = new QPushButton(bookmarksPage);
        bookmarksButton->setObjectName(QString::fromUtf8("bookmarksButton"));

        FindBookmarkLayout->addWidget(bookmarksButton);


        gridLayout_9->addLayout(FindBookmarkLayout, 1, 0, 1, 1);

        label_11 = new QLabel(bookmarksPage);
        label_11->setObjectName(QString::fromUtf8("label_11"));
        label_11->setWordWrap(true);

        gridLayout_9->addWidget(label_11, 2, 0, 1, 1);

        groupBox_5 = new QGroupBox(bookmarksPage);
        groupBox_5->setObjectName(QString::fromUtf8("groupBox_5"));
        gridLayout_10 = new QGridLayout(groupBox_5);
        gridLayout_10->setObjectName(QString::fromUtf8("gridLayout_10"));
        bookmarkPlainEdit = new QPlainTextEdit(groupBox_5);
        bookmarkPlainEdit->setObjectName(QString::fromUtf8("bookmarkPlainEdit"));
        QFont font;
        font.setFamily(QString::fromUtf8("Bera Sans Mono [bitstream]"));
        font.setPointSize(11);
        bookmarkPlainEdit->setFont(font);
        bookmarkPlainEdit->setLineWrapMode(QPlainTextEdit::NoWrap);

        gridLayout_10->addWidget(bookmarkPlainEdit, 0, 0, 1, 1);


        gridLayout_9->addWidget(groupBox_5, 3, 0, 1, 1);

        stackedWidget->addWidget(bookmarksPage);

        gridLayout_4->addWidget(stackedWidget, 0, 1, 1, 1);

        buttonBox = new QDialogButtonBox(PropertiesDialog);
        buttonBox->setObjectName(QString::fromUtf8("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Apply|QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        gridLayout_4->addWidget(buttonBox, 1, 0, 1, 2);

#if QT_CONFIG(shortcut)
        fontLabel->setBuddy(changeFontButton);
        label_5->setBuddy(colorSchemaCombo);
        label_6->setBuddy(styleComboBox);
        label_7->setBuddy(scrollBarPos_comboBox);
        label_8->setBuddy(tabsPos_comboBox);
        label_12->setBuddy(keybCursorShape_comboBox);
        label_4->setBuddy(appTransparencyBox);
        label->setBuddy(termTransparencyBox);
        label_9->setBuddy(terminalPresetComboBox);
        label_15->setBuddy(terminalMarginSpinBox);
        label_3->setBuddy(motionAfterPasting_comboBox);
        dropHeightLabel->setBuddy(dropHeightSpinBox);
        dropWidthLabel->setBuddy(dropWidthSpinBox);
        label_10->setBuddy(bookmarksLineEdit);
#endif // QT_CONFIG(shortcut)

        retranslateUi(PropertiesDialog);
        QObject::connect(buttonBox, SIGNAL(accepted()), PropertiesDialog, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), PropertiesDialog, SLOT(reject()));
        QObject::connect(listWidget, SIGNAL(currentRowChanged(int)), stackedWidget, SLOT(setCurrentIndex(int)));
        QObject::connect(fixedTabWidthCheckBox, SIGNAL(toggled(bool)), fixedTabWidthSpinBox, SLOT(setEnabled(bool)));
        QObject::connect(historyUnlimited, SIGNAL(toggled(bool)), historyLimitedTo, SLOT(setDisabled(bool)));

        stackedWidget->setCurrentIndex(0);
        termComboBox->setCurrentIndex(1);


        QMetaObject::connectSlotsByName(PropertiesDialog);
    } // setupUi

    void retranslateUi(QDialog *PropertiesDialog)
    {
        PropertiesDialog->setWindowTitle(QCoreApplication::translate("PropertiesDialog", "Terminal settings", nullptr));

        const bool __sortingEnabled = listWidget->isSortingEnabled();
        listWidget->setSortingEnabled(false);
        QListWidgetItem *___qlistwidgetitem = listWidget->item(0);
        ___qlistwidgetitem->setText(QCoreApplication::translate("PropertiesDialog", "Appearance", nullptr));
        QListWidgetItem *___qlistwidgetitem1 = listWidget->item(1);
        ___qlistwidgetitem1->setText(QCoreApplication::translate("PropertiesDialog", "Behavior", nullptr));
        QListWidgetItem *___qlistwidgetitem2 = listWidget->item(2);
        ___qlistwidgetitem2->setText(QCoreApplication::translate("PropertiesDialog", "Shortcuts", nullptr));
        QListWidgetItem *___qlistwidgetitem3 = listWidget->item(3);
        ___qlistwidgetitem3->setText(QCoreApplication::translate("PropertiesDialog", "Dropdown", nullptr));
        QListWidgetItem *___qlistwidgetitem4 = listWidget->item(4);
        ___qlistwidgetitem4->setText(QCoreApplication::translate("PropertiesDialog", "Bookmarks", nullptr));
        listWidget->setSortingEnabled(__sortingEnabled);

        fontLabel->setText(QCoreApplication::translate("PropertiesDialog", "Font", nullptr));
        fontSampleLabel->setText(QString());
        changeFontButton->setText(QCoreApplication::translate("PropertiesDialog", "&Change...", nullptr));
        label_5->setText(QCoreApplication::translate("PropertiesDialog", "Color scheme", nullptr));
        label_6->setText(QCoreApplication::translate("PropertiesDialog", "Widget style", nullptr));
        label_7->setText(QCoreApplication::translate("PropertiesDialog", "Scrollbar position", nullptr));
        label_8->setText(QCoreApplication::translate("PropertiesDialog", "Tabs position", nullptr));
        label_12->setText(QCoreApplication::translate("PropertiesDialog", "Cursor shape", nullptr));
#if QT_CONFIG(tooltip)
        boldIntenseCheckBox->setToolTip(QCoreApplication::translate("PropertiesDialog", "Toggles usage of bold font face for rendering intense colors", nullptr));
#endif // QT_CONFIG(tooltip)
        boldIntenseCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Use bold font face for intense colors", nullptr));
#if QT_CONFIG(tooltip)
        menuAccelCheckBox->setToolTip(QCoreApplication::translate("PropertiesDialog", "Accelerators are activated by Alt and can interfere with the terminal.", nullptr));
#endif // QT_CONFIG(tooltip)
        menuAccelCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "No menu bar accelerator", nullptr));
        showMenuCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Show the menu bar", nullptr));
        borderlessCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "&Hide Window Borders", nullptr));
        hideTabBarCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Hide tab bar with only one tab", nullptr));
        fixedTabWidthCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Fixed tab width:", nullptr));
        fixedTabWidthSpinBox->setSuffix(QCoreApplication::translate("PropertiesDialog", "px", nullptr));
        highlightCurrentCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Show a border around the current terminal", nullptr));
        closeTabButtonCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Show close button on each tab", nullptr));
        changeWindowTitleCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Change window title based on current terminal", nullptr));
        changeWindowIconCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Change window icon based on current terminal", nullptr));
        showTerminalSizeHintCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Show terminal size on resize", nullptr));
        enabledBidiSupportCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Enable bi-directional text support", nullptr));
#if QT_CONFIG(tooltip)
        useFontBoxDrawingCharsCheckBox->setToolTip(QCoreApplication::translate("PropertiesDialog", "Specify whether box drawing characters should be drawn by QTerminal internally or left to underlying font rendering libraries.", nullptr));
#endif // QT_CONFIG(tooltip)
        useFontBoxDrawingCharsCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Use box drawing characters contained in the font", nullptr));
        label_4->setText(QCoreApplication::translate("PropertiesDialog", "Application transparency", nullptr));
        appTransparencyBox->setSuffix(QCoreApplication::translate("PropertiesDialog", " %", nullptr));
        label->setText(QCoreApplication::translate("PropertiesDialog", "Terminal transparency", nullptr));
        termTransparencyBox->setSuffix(QCoreApplication::translate("PropertiesDialog", " %", nullptr));
        label_13->setText(QCoreApplication::translate("PropertiesDialog", "Background image:", nullptr));
        chooseBackgroundImageButton->setText(QCoreApplication::translate("PropertiesDialog", "Select", nullptr));
        label_16->setText(QCoreApplication::translate("PropertiesDialog", "Background mode:", nullptr));
        backgroundModecomboBox->setItemText(0, QCoreApplication::translate("PropertiesDialog", "None", nullptr));
        backgroundModecomboBox->setItemText(1, QCoreApplication::translate("PropertiesDialog", "Stretch", nullptr));
        backgroundModecomboBox->setItemText(2, QCoreApplication::translate("PropertiesDialog", "Zoom", nullptr));
        backgroundModecomboBox->setItemText(3, QCoreApplication::translate("PropertiesDialog", "Fit", nullptr));
        backgroundModecomboBox->setItemText(4, QCoreApplication::translate("PropertiesDialog", "Center", nullptr));

        label_9->setText(QCoreApplication::translate("PropertiesDialog", "Start with preset:", nullptr));
        terminalPresetComboBox->setItemText(0, QCoreApplication::translate("PropertiesDialog", "None (single terminal)", nullptr));
        terminalPresetComboBox->setItemText(1, QCoreApplication::translate("PropertiesDialog", "2 terminals horizontally", nullptr));
        terminalPresetComboBox->setItemText(2, QCoreApplication::translate("PropertiesDialog", "2 terminals vertically", nullptr));
        terminalPresetComboBox->setItemText(3, QCoreApplication::translate("PropertiesDialog", "4 terminals", nullptr));

        label_15->setText(QCoreApplication::translate("PropertiesDialog", "Terminal margin", nullptr));
        terminalMarginSpinBox->setSuffix(QCoreApplication::translate("PropertiesDialog", "px", nullptr));
        groupBox_2->setTitle(QCoreApplication::translate("PropertiesDialog", "Behavior", nullptr));
        historyLimited->setText(QCoreApplication::translate("PropertiesDialog", "History size (in lines)", nullptr));
        historyUnlimited->setText(QCoreApplication::translate("PropertiesDialog", "Unlimited history", nullptr));
        label_3->setText(QCoreApplication::translate("PropertiesDialog", "Action after paste", nullptr));
        label_14->setText(QCoreApplication::translate("PropertiesDialog", "Default $TERM", nullptr));
#if QT_CONFIG(tooltip)
        disableBracketedPasteModeCheckBox->setToolTip(QCoreApplication::translate("PropertiesDialog", "Bracketed paste mode is useful for pasting multiline strings.", nullptr));
#endif // QT_CONFIG(tooltip)
        disableBracketedPasteModeCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Forcefully disable bracketed paste mode", nullptr));
        confirmMultilinePasteCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Confirm multiline paste", nullptr));
        trimPastedTrailingNewlinesCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Trim trailing newlines in pasted text", nullptr));
        closeTabOnMiddleClickCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Close tab on middle-click", nullptr));
        askOnExitCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Ask for confirmation when closing", nullptr));
        savePosOnExitCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Save Position when closing", nullptr));
        saveSizeOnExitCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Save Size when closing", nullptr));
        fixedSizeLabel->setText(QCoreApplication::translate("PropertiesDialog", "Start with this size:", nullptr));
        fixedWithSpinBox->setSuffix(QCoreApplication::translate("PropertiesDialog", " px", nullptr));
        fixedHeightSpinBox->setSuffix(QCoreApplication::translate("PropertiesDialog", " px", nullptr));
        getCurrentSizeButton->setText(QCoreApplication::translate("PropertiesDialog", "Get current size", nullptr));
        useCwdCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Open new terminals in current working directory", nullptr));
#if QT_CONFIG(tooltip)
        openNewTabRightToActiveTabCheckBox->setToolTip(QCoreApplication::translate("PropertiesDialog", "If unchecked the new tab will be opened as the rightmost tab", nullptr));
#endif // QT_CONFIG(tooltip)
        openNewTabRightToActiveTabCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Open new tab to the right of the active tab", nullptr));
        audibleBellCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Audible bell", nullptr));

#if QT_CONFIG(tooltip)
        label_17->setToolTip(QCoreApplication::translate("PropertiesDialog", "This command will be run with an argument containing the file name of a tempfile containing the scrollback history", nullptr));
#endif // QT_CONFIG(tooltip)
        label_17->setText(QCoreApplication::translate("PropertiesDialog", "Handle history command", nullptr));
        groupBox_4->setTitle(QCoreApplication::translate("PropertiesDialog", "Emulation", nullptr));
        label_2->setText(QCoreApplication::translate("PropertiesDialog", "<html><head/><body><p>Which behavior to emulate. Note that this does not have to match your operating system.</p><p>If you are not sure, use the <span style=\" font-weight:600;\">default</span> emulation.</p></body></html>", nullptr));
        QTableWidgetItem *___qtablewidgetitem = shortcutsWidget->horizontalHeaderItem(0);
        ___qtablewidgetitem->setText(QCoreApplication::translate("PropertiesDialog", "Shortcut", nullptr));
        QTableWidgetItem *___qtablewidgetitem1 = shortcutsWidget->horizontalHeaderItem(1);
        ___qtablewidgetitem1->setText(QCoreApplication::translate("PropertiesDialog", "Key", nullptr));
#if QT_CONFIG(tooltip)
        shortcutsWidget->setToolTip(QCoreApplication::translate("PropertiesDialog", "To edit a Shortcut:\n"
"1. Double-click its Key\n"
"2. Press the desired combination and release it\n"
"3. Click on a Shortcut or press Enter\n"
"\n"
"To remove/disable a Shortcut, at point 2 press only a modifier (like Shift)", nullptr));
#endif // QT_CONFIG(tooltip)
        dropShowOnStartCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Show on start", nullptr));
#if QT_CONFIG(tooltip)
        dropKeepOpenCheckBox->setToolTip(QCoreApplication::translate("PropertiesDialog", "A lock button is shown on horizontal tab bar", nullptr));
#endif // QT_CONFIG(tooltip)
        dropKeepOpenCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Keep window open when it loses focus", nullptr));
        groupBox_3->setTitle(QCoreApplication::translate("PropertiesDialog", "Size", nullptr));
        dropHeightLabel->setText(QCoreApplication::translate("PropertiesDialog", "Height", nullptr));
        dropHeightSpinBox->setSuffix(QCoreApplication::translate("PropertiesDialog", "%", nullptr));
        dropWidthLabel->setText(QCoreApplication::translate("PropertiesDialog", "Width", nullptr));
        dropWidthSpinBox->setSuffix(QCoreApplication::translate("PropertiesDialog", "%", nullptr));
        dropShortCutLabel->setText(QCoreApplication::translate("PropertiesDialog", "Shortcut:", nullptr));
        useBookmarksCheckBox->setText(QCoreApplication::translate("PropertiesDialog", "Enable bookmarks", nullptr));
        label_10->setText(QCoreApplication::translate("PropertiesDialog", "Bookmark file", nullptr));
        bookmarksButton->setText(QCoreApplication::translate("PropertiesDialog", "Find...", nullptr));
        label_11->setText(QCoreApplication::translate("PropertiesDialog", "You can specify your own bookmarks file location. It allows easy bookmark sharing with tools like OwnCloud or Dropbox.", nullptr));
        groupBox_5->setTitle(QCoreApplication::translate("PropertiesDialog", "Edit bookmark file contents", nullptr));
    } // retranslateUi

};

namespace Ui {
    class PropertiesDialog: public Ui_PropertiesDialog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PROPERTIESDIALOG_H
