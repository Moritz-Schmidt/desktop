/*
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
 * Copyright (C) by Krzesimir Nowak <krzesimir@endocode.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "account.h"
#include "config.h"
#include "configfile.h"
#include "theme.h"
#include "owncloudgui.h"

#include "wizard/owncloudwizard.h"
#include "wizard/welcomepage.h"
#include "wizard/owncloudsetuppage.h"
#include "wizard/owncloudhttpcredspage.h"
#include "wizard/owncloudadvancedsetuppage.h"
#include "wizard/webviewpage.h"
#include "wizard/flow2authcredspage.h"

#include "common/vfs.h"

#include "QProgressIndicator.h"

#include <QtCore>
#include <QtGui>
#include <QMessageBox>
#include <owncloudgui.h>

#include <cstdlib>

namespace OCC {

Q_LOGGING_CATEGORY(lcWizard, "nextcloud.gui.wizard", QtInfoMsg)

OwncloudWizard::OwncloudWizard(QWidget *parent)
    : QWizard(parent)
    , _account(nullptr)
    , _welcomePage(new WelcomePage(this))
    , _setupPage(new OwncloudSetupPage(this))
    , _httpCredsPage(new OwncloudHttpCredsPage(this))
    , _flow2CredsPage(new Flow2AuthCredsPage)
    , _advancedSetupPage(new OwncloudAdvancedSetupPage(this))
#ifdef WITH_WEBENGINE
    , _webViewPage(new WebViewPage(this))
#else // WITH_WEBENGINE
    , _webViewPage(nullptr)
#endif // WITH_WEBENGINE
{
    setObjectName("owncloudWizard");

    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setPage(WizardCommon::Page_Welcome, _welcomePage);
    setPage(WizardCommon::Page_ServerSetup, _setupPage);
    setPage(WizardCommon::Page_HttpCreds, _httpCredsPage);
    setPage(WizardCommon::Page_Flow2AuthCreds, _flow2CredsPage);
    setPage(WizardCommon::Page_AdvancedSetup, _advancedSetupPage);
#ifdef WITH_WEBENGINE
    setPage(WizardCommon::Page_WebView, _webViewPage);
#endif // WITH_WEBENGINE

    connect(this, &QDialog::finished, this, &OwncloudWizard::basicSetupFinished);

    // note: start Id is set by the calling class depending on if the
    // welcome text is to be shown or not.

    connect(this, &QWizard::currentIdChanged, this, &OwncloudWizard::slotCurrentPageChanged);
    connect(_setupPage, &OwncloudSetupPage::determineAuthType, this, &OwncloudWizard::determineAuthType);
    connect(_httpCredsPage, &OwncloudHttpCredsPage::connectToOCUrl, this, &OwncloudWizard::connectToOCUrl);
    connect(_flow2CredsPage, &Flow2AuthCredsPage::connectToOCUrl, this, &OwncloudWizard::connectToOCUrl);
#ifdef WITH_WEBENGINE
    connect(_webViewPage, &WebViewPage::connectToOCUrl, this, &OwncloudWizard::connectToOCUrl);
#endif // WITH_WEBENGINE
    connect(_advancedSetupPage, &OwncloudAdvancedSetupPage::createLocalAndRemoteFolders,
        this, &OwncloudWizard::createLocalAndRemoteFolders);
    connect(this, &QWizard::customButtonClicked, this, &OwncloudWizard::slotCustomButtonClicked);


    Theme *theme = Theme::instance();
    setWindowTitle(tr("Add %1 account").arg(theme->appNameGUI()));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnStartPage);
    setOption(QWizard::NoCancelButton);
    setButtonText(QWizard::CustomButton1, tr("Skip folders configuration"));
    setButtonText(QWizard::CustomButton2, tr("Cancel"));

    // Change the next buttons size policy since we hide it on the
    // welcome page but want it to fill it's space that we don't get
    // flickering when the page changes
    auto nextButtonSizePolicy = button(QWizard::NextButton)->sizePolicy();
    nextButtonSizePolicy.setRetainSizeWhenHidden(true);
    button(QWizard::NextButton)->setSizePolicy(nextButtonSizePolicy);

    // Connect styleChanged events to our widgets, so they can adapt (Dark-/Light-Mode switching)
    connect(this, &OwncloudWizard::styleChanged, _setupPage, &OwncloudSetupPage::slotStyleChanged);
    connect(this, &OwncloudWizard::styleChanged, _advancedSetupPage, &OwncloudAdvancedSetupPage::slotStyleChanged);
    connect(this, &OwncloudWizard::styleChanged, _flow2CredsPage, &Flow2AuthCredsPage::slotStyleChanged);

    customizeStyle();

    // allow Flow2 page to poll on window activation
    connect(this, &OwncloudWizard::onActivate, _flow2CredsPage, &Flow2AuthCredsPage::slotPollNow);

    adjustWizardSize();
    centerWindow();
}

void OwncloudWizard::centerWindow()
{
    const auto wizardWindow = window();
    const auto screen = QGuiApplication::screenAt(wizardWindow->pos())
        ? QGuiApplication::screenAt(wizardWindow->pos())
        : QGuiApplication::primaryScreen();
    const auto screenGeometry = screen->geometry();
    const auto windowGeometry = wizardWindow->geometry();
    const auto newWindowPosition = screenGeometry.center() - QPoint(windowGeometry.width() / 2, windowGeometry.height() / 2);
    wizardWindow->move(newWindowPosition);
}


void OwncloudWizard::adjustWizardSize()
{
    const auto pageSizes = calculateWizardPageSizes();
    const auto currentPageIndex = currentId();

    // If we can, just use the size of the current page
    if(currentPageIndex > -1 && currentPageIndex < pageSizes.count()) {
        resize(pageSizes.at(currentPageIndex));
        return;
    }

    // As a backup, resize to largest page
    resize(calculateLargestSizeOfWizardPages(pageSizes));
}

QList<QSize> OwncloudWizard::calculateWizardPageSizes() const
{
    QList<QSize> pageSizes;
    const auto pIds = pageIds();

    std::transform(pIds.cbegin(), pIds.cend(), std::back_inserter(pageSizes), [this](int pageId) {
        auto p = page(pageId);
        p->adjustSize();
        return p->sizeHint();
    });

    return pageSizes;
}

QSize OwncloudWizard::calculateLargestSizeOfWizardPages(const QList<QSize> &pageSizes) const
{
    QSize largestSize;
    for(const auto size : pageSizes) {
        const auto largerWidth = qMax(largestSize.width(), size.width());
        const auto largerHeight = qMax(largestSize.height(), size.height());

        largestSize = QSize(largerWidth, largerHeight);
    }

    return largestSize;
}

void OwncloudWizard::setAccount(AccountPtr account)
{
    _account = account;
}

AccountPtr OwncloudWizard::account() const
{
    return _account;
}

QString OwncloudWizard::localFolder() const
{
    return (_advancedSetupPage->localFolder());
}

QStringList OwncloudWizard::selectiveSyncBlacklist() const
{
    return _advancedSetupPage->selectiveSyncBlacklist();
}

bool OwncloudWizard::useVirtualFileSync() const
{
    return _advancedSetupPage->useVirtualFileSync();
}

bool OwncloudWizard::isConfirmBigFolderChecked() const
{
    return _advancedSetupPage->isConfirmBigFolderChecked();
}

QString OwncloudWizard::ocUrl() const
{
    QString url = field("OCUrl").toString().simplified();
    return url;
}

bool OwncloudWizard::registration()
{
    return _registration;
}

void OwncloudWizard::setRegistration(bool registration)
{
    _registration = registration;
}

void OwncloudWizard::setRemoteFolder(const QString &remoteFolder)
{
    _advancedSetupPage->setRemoteFolder(remoteFolder);
}

void OwncloudWizard::successfulStep()
{
    const int id(currentId());

    switch (id) {
    case WizardCommon::Page_HttpCreds:
        _httpCredsPage->setConnected();
        break;

    case WizardCommon::Page_Flow2AuthCreds:
        _flow2CredsPage->setConnected();
        break;

#ifdef WITH_WEBENGINE
    case WizardCommon::Page_WebView:
        _webViewPage->setConnected();
        break;
#endif // WITH_WEBENGINE

    case WizardCommon::Page_AdvancedSetup:
        _advancedSetupPage->directoriesCreated();
        break;

    case WizardCommon::Page_ServerSetup:
        qCWarning(lcWizard, "Should not happen at this stage.");
        break;
    }

    ownCloudGui::raiseDialog(this);
    if (nextId() == -1) {
        disconnect(this, &QDialog::finished, this, &OwncloudWizard::basicSetupFinished);
        emit basicSetupFinished(QDialog::Accepted);
    } else {
        next();
    }
}

void OwncloudWizard::slotCustomButtonClicked(const int which)
{
    if (which == WizardButton::CustomButton1) {
        // This is the 'Skip folders configuration' button
        Q_EMIT skipFolderConfiguration();
    } else if (which == WizardButton::CustomButton2) {
        // Because QWizard doesn't have a way of directly going to a specific page (!!!)
        restart();
    }
}

void OwncloudWizard::setAuthType(DetermineAuthTypeJob::AuthType type)
{
    _setupPage->setAuthType(type);

    if (type == DetermineAuthTypeJob::LoginFlowV2) {
        _credentialsPage = _flow2CredsPage;
#ifdef WITH_WEBENGINE
    } else if (type == DetermineAuthTypeJob::WebViewFlow) {
        _credentialsPage = _webViewPage;
#endif // WITH_WEBENGINE
    } else { // try Basic auth even for "Unknown"
        _credentialsPage = _httpCredsPage;
    }
    next();
}

// TODO: update this function
void OwncloudWizard::slotCurrentPageChanged(int id)
{
    qCDebug(lcWizard) << "Current Wizard page changed to " << id;

    const auto setNextButtonAsDefault = [this]() {
        auto nextButton = qobject_cast<QPushButton *>(button(QWizard::NextButton));
        if (nextButton) {
            nextButton->setDefault(true);
        }
    };

    if (id == WizardCommon::Page_Welcome) {
        // Set next button to just hidden so it retains it's layout
        button(QWizard::NextButton)->setHidden(true);
        // Need to set it from here, otherwise it has no effect
        _welcomePage->setLoginButtonDefault();
    } else if (
#ifdef WITH_WEBENGINE
        id == WizardCommon::Page_WebView ||
#endif // WITH_WEBENGINE
        id == WizardCommon::Page_Flow2AuthCreds) {
        setButtonLayout({ QWizard::BackButton, QWizard::Stretch });
    } else if (id == WizardCommon::Page_AdvancedSetup) {
        setButtonLayout({ QWizard::CustomButton2, QWizard::Stretch, QWizard::CustomButton1, QWizard::FinishButton });
        setNextButtonAsDefault();
    } else {
        setButtonLayout({ QWizard::BackButton, QWizard::Stretch, QWizard::NextButton });
        setNextButtonAsDefault();
    }

    if (id == WizardCommon::Page_ServerSetup) {
        emit clearPendingRequests();
    }

    if (id == WizardCommon::Page_AdvancedSetup && _credentialsPage == _flow2CredsPage) {
        // Disable the back button in the Page_AdvancedSetup because we don't want
        // to re-open the browser.
        button(QWizard::BackButton)->setEnabled(false);
    }
}

void OwncloudWizard::displayError(const QString &msg, bool retryHTTPonly)
{
    switch (currentId()) {
    case WizardCommon::Page_ServerSetup:
        _setupPage->setErrorString(msg, retryHTTPonly);
        break;

    case WizardCommon::Page_HttpCreds:
        _httpCredsPage->setErrorString(msg);
        break;

    case WizardCommon::Page_AdvancedSetup:
        _advancedSetupPage->setErrorString(msg);
        break;
    }
}

void OwncloudWizard::appendToConfigurationLog(const QString &msg, LogType /*type*/)
{
    _setupLog << msg;
    qCDebug(lcWizard) << "Setup-Log: " << msg;
}

void OwncloudWizard::setOCUrl(const QString &url)
{
    _setupPage->setServerUrl(url);
}

AbstractCredentials *OwncloudWizard::getCredentials() const
{
    if (_credentialsPage) {
        return _credentialsPage->getCredentials();
    }

    return nullptr;
}

void OwncloudWizard::changeEvent(QEvent *e)
{
    switch (e->type()) {
    case QEvent::StyleChange:
    case QEvent::PaletteChange:
    case QEvent::ThemeChange:
        customizeStyle();

        // Notify the other widgets (Dark-/Light-Mode switching)
        emit styleChanged();
        break;
    case QEvent::ActivationChange:
        if (isActiveWindow())
            emit onActivate();
        break;
    default:
        break;
    }

    QWizard::changeEvent(e);
}

void OwncloudWizard::customizeStyle()
{
    // HINT: Customize wizard's own style here, if necessary in the future (Dark-/Light-Mode switching)

    // Set background colors
    auto wizardPalette = palette();
    const auto backgroundColor = wizardPalette.color(QPalette::Window);
    wizardPalette.setColor(QPalette::Base, backgroundColor);
    // Set separator color
    wizardPalette.setColor(QPalette::Mid, backgroundColor);

    setPalette(wizardPalette);
}

void OwncloudWizard::bringToTop()
{
    // bring wizard to top
    ownCloudGui::raiseDialog(this);
}

void OwncloudWizard::askExperimentalVirtualFilesFeature(QWidget *receiver, const std::function<void(bool enable)> &callback)
{
    const auto bestVfsMode = bestAvailableVfsMode();
    QMessageBox *msgBox = nullptr;
    QPushButton *acceptButton = nullptr;
    switch (bestVfsMode) {
    case Vfs::WindowsCfApi:
        callback(true);
        return;
    case Vfs::WithSuffix:
        msgBox = new QMessageBox(
            QMessageBox::Warning,
            tr("Enable experimental feature?"),
            tr("When the \"virtual files\" mode is enabled no files will be downloaded initially. "
               "Instead, a tiny \"%1\" file will be created for each file that exists on the server. "
               "The contents can be downloaded by running these files or by using their context menu."
               "\n\n"
               "The virtual files mode is mutually exclusive with selective sync. "
               "Currently unselected folders will be translated to online-only folders "
               "and your selective sync settings will be reset."
               "\n\n"
               "Switching to this mode will abort any currently running synchronization."
               "\n\n"
               "This is a new, experimental mode. If you decide to use it, please report any "
               "issues that come up.")
                .arg(APPLICATION_DOTVIRTUALFILE_SUFFIX),
            QMessageBox::NoButton, receiver);
        acceptButton = msgBox->addButton(tr("Enable experimental placeholder mode"), QMessageBox::AcceptRole);
        msgBox->addButton(tr("Stay safe"), QMessageBox::RejectRole);
        break;
    case Vfs::XAttr:
    case Vfs::Off:
        Q_UNREACHABLE();
    }

    connect(msgBox, &QMessageBox::accepted, receiver, [callback, msgBox, acceptButton] {
        callback(msgBox->clickedButton() == acceptButton);
        msgBox->deleteLater();
    });
    msgBox->open();
}

} // end namespace
