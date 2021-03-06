
#include "main.h"
#include <QDebug>
#include <QCompleter>
#include <QAbstractListModel>


#include <QModelIndex>
#include <QTextStream>
#include <QStringList>
#include <QPixmap>
#include <QMessageBox>
#include <QMenu>
#include <QProcess>
#include <QLightDM/UsersModel>
#include <QMetaMethod>
#include <QNetworkInterface>
#include <QTimer>
#include <QWebEngineView>
#include <QPushButton>
#include <QMovie>
#include <QShortcut>
#include <QToolButton>
#include <QPropertyAnimation>
#include <QProcess>
#include <qcoreevent.h>



#include "loginform.h"
#include "ui_loginform.h"
#include "settings.h"
#include "dialog_webview.h"
#include <pwd.h>
#include "settingsform.h"
#include "mainwindow.h"


const int KeyRole = QLightDM::SessionsModel::KeyRole;

int rows(QAbstractItemModel& model) {
    return model.rowCount(QModelIndex());
}

QString displayData(QAbstractItemModel& model, int row, int role)
{
    QModelIndex modelIndex = model.index(row, 0);
    return model.data(modelIndex, role).toString();
}

LoginForm::LoginForm(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::LoginForm),
    m_Greeter(),
    power(this),
    sessionsModel(QLightDM::SessionsModel::LocalSessions,this),
    userModel(this)

{

    m_Greeter.setResettable(false);

    if (!m_Greeter.connectSync()) {
        qCritical() << "Failed to connect lightdm";
        close();
    }

    ui->setupUi(this);
    initialize();

}

LoginForm::~LoginForm()
{
    delete ui;
    delete mv;
}

void LoginForm::setFocus(Qt::FocusReason reason)
{
    if (ui->userInput->text().isEmpty()) {
        ui->userInput->setFocus(reason);
    } else {
        ui->passwordInput->setFocus(reason);

    }
}


void LoginForm::initialize()
{

#ifdef SCREENKEYBOARD
    connect(qApp, SIGNAL(focusChanged(QWidget*,QWidget*)), this, SLOT(focusChanged(QWidget*,QWidget*)));
#endif

    ui->pushButton_sr->hide();

    ui->hostnameLabel->setText(m_Greeter.hostname());
    mv = new QMovie(":/resources/load1.gif");
    mv->setScaledSize(QSize(ui->giflabel->width(), ui->giflabel->height()));

    QString imagepath = Settings().logopath();

    if(imagepath.isNull()){


        QPixmap pxmap = QIcon::fromTheme("avatar-default", QIcon(":/resources/avatar-default.png")).pixmap(ui->logolabel->width(),ui->logolabel->height());
        ui->logolabel->setPixmap(pxmap);

    }else{

        QPixmap logoicon(imagepath);
        ui->logolabel->setPixmap(logoicon.scaled(ui->logolabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    setCurrentSession(m_Greeter.defaultSessionHint());

    connect(&m_Greeter, SIGNAL(showPrompt(QString, QLightDM::Greeter::PromptType)), this, SLOT(onPrompt(QString, QLightDM::Greeter::PromptType)));
    connect(&m_Greeter, SIGNAL(showMessage(QString,QLightDM::Greeter::MessageType)), this, SLOT(onMessage(QString,QLightDM::Greeter::MessageType)));
    connect(&m_Greeter, SIGNAL(authenticationComplete()), this, SLOT(authenticationComplete()));
    connect(&m_Greeter, SIGNAL(reset()), this, SLOT(resetRequest()));




    ui->passwordInput->clear();

    initializeUserList();

    checkPasswordResetButton();

    if (!m_Greeter.hideUsersHint()) {
        QLightDM::UsersModel usersModel;

        for (int i = 0; i < usersModel.rowCount(QModelIndex()); i++) {
            knownUsers << usersModel.data(usersModel.index(i, 0), QLightDM::UsersModel::NameRole).toString();
        }

        ui->userInput->setCompleter(new QCompleter(knownUsers));
        ui->userInput->completer()->setCompletionMode(QCompleter::InlineCompletion);
    }

    QString user = Cache().getLastUser();

    if (user.isEmpty()) {
        user = m_Greeter.selectUserHint();
    }

    ui->userInput->setText(user);
    // cancelLogin();

    messageReceived = false;
    needPasswordChange = false;
    needReenterOldPassword = false;
    loginStartFlag = false;
    resetStartFlag = false;
    loginTimeot = 0;
    networkOK = false;
    loginprompt = false;
    nwcheckcount = 0;

    loginTimer = new QTimer();
    resetTimer = new QTimer();
    animationTimer = new QTimer();

    connect(loginTimer, SIGNAL(timeout()), this, SLOT(LoginTimerFinished()));
    connect(resetTimer, SIGNAL(timeout()), this, SLOT(passwordResetTimerFinished()));
    connect(animationTimer, SIGNAL(timeout()), this, SLOT(animationTimerFinished()));

    ui->giflabel->setAttribute(Qt::WA_NoSystemBackground);
    ui->giflabel->setMovie(mv);

    // ui->pwShowbutton->setParent(ui->passwordInput);
    //ui->pwShowbutton->raise();
    //ui->pwShowbutton->move( ui->passwordInput->x() + ui->passwordInput->width() - 10, 5);


    if(Settings().waittimeout() == 0){

        if(Cache().getLastUser() != NULL){
            pageTransition(ui->userspage);

        }else{
            qInfo() << "No cached last user is found. No need to show users page";
            ui->userInput->show();
            ui->userLabel->hide();
            ui->userInput->setFocus();

            pageTransition(ui->loginpage);

        }
    }
    else{
        pageTransition(ui->waitpage);
        qInfo() << "Waiting for network and services";

    }

    QWidget::setTabOrder(ui->userInput, ui->passwordInput);
    QWidget::setTabOrder(ui->oldpasswordinput, ui->newpasswordinput);
    QWidget::setTabOrder(ui->newpasswordinput, ui->newpasswordconfirminput);

}

void LoginForm::cancelLogin()
{

    if (m_Greeter.inAuthentication()) {
        m_Greeter.cancelAuthentication();
        qWarning() << "Authentication is canceled";
       // ui->messagelabel->clear();
    }

}


void LoginForm::startLogin()
{

    if(ui->userInput->text().isEmpty())
        return;

    ui->messagelabel->clear();
    tmpPassword = ui->passwordInput->text();
    oldPassword = tmpPassword;
    ui->passwordInput->setEnabled(false);
    ui->userInput->setEnabled(false);

    loginTimer->setTimerType(Qt::TimerType::CoarseTimer);
    loginTimer->setSingleShot(true);
    loginTimer->setInterval(500);
    loginTimerState = 0;
    loginTimer->start();

}


void LoginForm::onPrompt(QString prompt, QLightDM::Greeter::PromptType promptType)
{
    promptFlag = true;
    lastPrompt = prompt;

    //"password: "
    //"Enter new password: "
    //"Enter it again: "

    qInfo() << "Received Prompt: " + prompt + " type: " + QString::number(promptType);

    if((prompt.compare("Enter new password: ") == 0 || prompt.compare("New password: ") == 0 ||
        prompt.compare("(current) UNIX password: ") == 0 || prompt.compare("Current Password: ") == 0)
            && needPasswordChange != 1 && !resetStartFlag){

        if(prompt.compare("(current) UNIX password: " ) == 0 || prompt.compare("Current Password: ") == 0)
            needReenterOldPassword = 1;

        needPasswordChange = true;
        pageTransition(ui->warningpage);
        oldPassword = tmpPassword;
        ui->warninglabel->setText(tr("Your Password is Expired. You have to change it"));
        ui->newpasswordinput->setEnabled(true);
        ui->newpasswordconfirminput->setEnabled(true);
        ui->newpasswordinput->clear();
        ui->newpasswordconfirminput->clear();
        ui->oldpasswordinput->setText(oldPassword);
        ui->oldpasswordinput->setEnabled(true);
        ui->acceptbutton->setFocus();

    }
    else if((prompt.compare("Enter new password: ") == 0 || prompt.compare("New password: ") == 0 ) &&
            resetStartFlag && !needReenterOldPassword){

        needPasswordChange = true;
        pageTransition(ui->resetpage);
        ui->oldpasswordinput->setEnabled(true);
        ui->newpasswordinput->setEnabled(true);
        ui->newpasswordconfirminput->setEnabled(true);
        ui->newpasswordinput->clear();
        ui->newpasswordconfirminput->clear();
        ui->newpasswordinput->setFocus();

        resetStartFlag = false;
        loginStartFlag = false;
    }else if((prompt.compare("login:") == 0 || prompt.compare("login: ") == 0 )){

        loginprompt = true;

    }
}

void LoginForm::onMessage(QString prompt, QLightDM::Greeter::MessageType messageType){
    QString type = NULL;

    qInfo() << "Received Message: " + prompt + " type: " + QString::number(messageType);

    if(messageType == QLightDM::Greeter::MessageTypeError){
        type = tr("Error");
    }
    else if(messageType == QLightDM::Greeter::MessageTypeInfo){
        type = tr("Info");
    }
    else{
        type = " ";
    }

    if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->loginpage)){
        messageReceived = true;
        ui->messagelabel->setText(type + " : " + prompt);
        ui->warninglabel->setText(type + " : " + prompt);
    }else if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->warningpage)){
        ui->warninglabel->setText(type + " : " + prompt);
    }else if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->resetpage) || resetStartFlag){
        ui->rstpwdmessagelabel->setText(type + " : " + prompt);
    }else{
        ui->messagelabel->setText(type + " : " + prompt);

    }

    lastMessage = prompt;

}


QString LoginForm::currentSession()
{
    return m_Greeter.defaultSessionHint();

    // QModelIndex index = sessionsModel.index(0, 0, QModelIndex());
    //return sessionsModel.data(index, QLightDM::SessionsModel::KeyRole).toString();
}


void LoginForm::setCurrentSession(QString session)
{
#if 0
    for (int i = 0; i < ui->sessionCombo->count(); i++) {
        if (session == sessionsModel.data(sessionsModel.index(i, 0), KeyRole).toString()) {
            ui->sessionCombo->setCurrentIndex(i);
            return;
        }
    }
#endif
}


void LoginForm::authenticationComplete()
{

    if (m_Greeter.isAuthenticated()) {

        qInfo() << "Authentication is completed for  " + ui->userInput->text();


        /* Reset to default screensaver values */

        needPasswordChange = 0;
        Cache().setLastUser(ui->userInput->text().trimmed());
        Cache().setLastSession(ui->userInput->text(), currentSession());
        addUsertoCache(ui->userInput->text().trimmed());
        Cache().sync();

        qWarning() <<  "Start session : " << currentSession();
        if(!m_Greeter.startSessionSync(currentSession())){

            qWarning() <<  "Failed to start session  ";

            if(!needPasswordChange)
                cancelLogin();

            if(!messageReceived)
                ui->messagelabel->setText(tr("Error : Login Incorrect"));
            else if(!lastMessage.isNull() && !lastMessage.isEmpty())
                 ui->messagelabel->setText(lastMessage);
            else
                  ui->messagelabel->setText(tr("Error : Login Incorrect"));
            preparetoLogin();

        }else{

        }
        needPasswordChange = false;

    }else if(loginStartFlag == true || resetStartFlag == true) {

        qWarning() <<  "Authentication error for  " + ui->userInput->text();

         qWarning() <<  "message: " + lastMessage;

        if(!needPasswordChange)
            cancelLogin();

        if(!messageReceived)
            ui->messagelabel->setText(tr("Error : Login Incorrect"));
        else if(!lastMessage.isNull() && !lastMessage.isEmpty())
             ui->messagelabel->setText(lastMessage);
        else
              ui->messagelabel->setText(tr("Error : Login Incorrect"));

        preparetoLogin();



    }


    messageReceived = false;
    loginStartFlag = false;
    resetStartFlag = false;
}


void LoginForm::preparetoLogin(){

    if(loginStartFlag)
        pageTransition(ui->loginpage);

    if(resetStartFlag)
        pageTransition(ui->resetpage);

    ui->passwordInput->clear();

    if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->loginpage)){
        ui->passwordInput->setEnabled(true);
        ui->userInput->setEnabled(true);
        ui->passwordInput->setFocus();
        ui->passwordInput->clear();
    }else if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->resetpage)){
        ui->newpasswordinput->clear();
        ui->oldpasswordinput->clear();
        ui->newpasswordconfirminput->clear();
        ui->newpasswordinput->setEnabled(true);
        ui->newpasswordconfirminput->setEnabled(true);
        ui->oldpasswordinput->setEnabled(true);
        ui->resetpasswordButton->setEnabled(true);
        ui->newpasswordinput->setFocus();
        needPasswordChange = false;
    }


}


void LoginForm::addUsertoCache(QString user_name){

    int i;
    int nullfound = 0;

    for(i = 0; i< 5; i++){
        if(userList[i].compare(user_name) == 0)
            userList[i].clear();

        if(i >= Settings().cachedusercount())
            userList[i].clear();

        if(userList[i].isNull() || userList[i].isEmpty())
            nullfound++;
    }

    if(userList[0].isNull() || userList[0].isEmpty()){
        userList[0] = user_name;
    }else{

        if(nullfound > 0){
            for(i = 4; i >= 0; i--){
                if((userList[i].isNull() || userList[i].isEmpty()) && i != 0){

                    userList[i] = userList[i-1];
                    userList[i-1].clear();

                }
            }

            userList[0] = user_name;

        }else{
            userList[4] = userList[3];
            userList[3] = userList[2];
            userList[2] = userList[1];
            userList[1] = userList[0];
            userList[0] = user_name;
        }

    }

    for(i = 0; i < Settings().cachedusercount(); i++){
        if(!userList[i].isNull() && !userList[i].isEmpty())
            Cache().setLastUsertoIndex(userList[i], i);
        else
            Cache().setLastUsertoIndex("", i);

    }

}


void LoginForm::on_pushButton_resetpwd_clicked()
{
    qInfo() << "Password reset webpage is opening";

    Dialog_webview dwview;
    dwview.setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);

    dwview.exec();
}


void LoginForm::checkPasswordResetButton(){

    uint password_key_selection = 0;

    QSettings greeterSettings(CONFIG_FILE, QSettings::IniFormat);

    if (greeterSettings.contains(PASSWORD_WEB_RESET_KEY)) {
        password_key_selection = greeterSettings.value(PASSWORD_WEB_RESET_KEY).toUInt();

    }

    if(!password_key_selection)
    {
        ui->pushButton_resetpwd->setVisible(false);
        ui->pushButton_resetpwd->setEnabled(false);
    }

}


void LoginForm::initializeUserList(){

    total_user_count = 0;

    for(int i = 0; i < Settings().cachedusercount(); i++){
        userList[i] = Cache().getLastUserfromIndex(i);

        if(!userList[i].isNull() && !userList[i].isEmpty() )
            total_user_count++;
    }

    for(int i = 0; i < 5; i++){
        if((userList[i].isNull() || userList[i].isEmpty()) && i < 4){

            userList[i] = userList[i + 1];
            userList[i + 1].clear();
        }
    }

    qInfo() <<  (QString::number(total_user_count) + " users found for last users cache");


    if(total_user_count == 0){
        return;
    }

    QString username;
    QString imagepath;
    QModelIndex modelIndex;

    for(int i = 0; i< total_user_count + 1; i++){

        for(int j = 0; j < userModel.rowCount(QModelIndex()); j++){

            modelIndex = userModel.index(j, 0);
            username = userModel.data(modelIndex, QLightDM::UsersModel::NameRole).toString();

            if(i != total_user_count){

                if(username.compare(userList[i]) == 0){

                    imagepath = userModel.data(modelIndex, QLightDM::UsersModel::ImagePathRole).toString();
                    break;
                }
            }
        }

        toolButtons[i] = new QToolButton(ui->usersframe);

        toolButtons[i]->setFocusPolicy(Qt::NoFocus);

        toolButtons[i]->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        toolButtons[i]->setStyleSheet(QString("font-size:%1px;").arg(TOOL_BUTTON_FONT_SIZE));


        if(imagepath.isNull() || imagepath.isEmpty() || i == total_user_count){


            if(i < total_user_count){
                QPixmap pxmap = QIcon::fromTheme("avatar-default",
                                                 QIcon(":/resources/avatar-default.png")).pixmap(LOGO_LABEL_SIZE, LOGO_LABEL_SIZE);

                toolButtons[i]->setIcon(pxmap);
            }
            else{

                QPixmap pm(TOOL_BUTTON_ICON_SIZE, TOOL_BUTTON_ICON_SIZE);

                pm.fill(qRgb(100,100,100));
                //pm.fill(qRgba(200,200,200,255));

                toolButtons[i]->setIcon(pm);

            }

        }else{
            QPixmap pxmap(imagepath);
            toolButtons[i]->setIcon(pxmap);
        }

        toolButtons[i]->setIconSize(QSize(TOOL_BUTTON_ICON_SIZE, TOOL_BUTTON_ICON_SIZE));

        /*   if(i == total_user_count)
            toolButtons[i]->setText(tr("Other User"));
        else
            toolButtons[i]->setText(userList[i]);
*/

        toolButtons[i]->setObjectName(QString("toolbutton%1").arg(i));

        connect(toolButtons[i], SIGNAL(clicked()), this, SLOT(userButtonClicked()));
    }


    toolButtons[0]->setStyleSheet(QString("background-color:rgba(200, 200, 200,0.9);\ncolor:black;\nfont-size:%1px;\nborder:2px solid rgb(255,202,8);").arg(TOOL_BUTTON_FONT_SIZE));


    usersbuttonReposition();

    current_user_button = 1;
    currentUserIndex = 0;
}

void LoginForm::usersbuttonReposition(){

    int middlepoint = ui->usersframe->width() / 2;
    int defaultframelength = ui->usersframe->width() / MAX_TOOL_BUTTON_COUNT;
    int totalframelength = defaultframelength * (total_user_count + 1);
    int framestart = middlepoint - (totalframelength / 2);
    int buttonx = 0;


    QString tmpuserList[5];

    for(int i = 0; i< total_user_count + 1; i++){

        buttonx = middlepoint - (TOOL_BUTTON_WIDTH / 2);

        if( i< total_user_count){

            tmpuserList[i] = userList[i];

            if(tmpuserList[i].size() > 12){
                tmpuserList[i].insert(12, "\n");

            }else if(tmpuserList[i].size() > 24){
                tmpuserList[i].insert(24, "\n");
            }
        }

        if(i == total_user_count)
            toolButtons[i]->setText(tr("Other User"));
        else
            toolButtons[i]->setText(tmpuserList[i]);



        toolButtons[i]->setGeometry(buttonx, TOOL_BUTTON_Y, TOOL_BUTTON_WIDTH, TOOL_BUTTON_HEIGHT);
        toolButtons[i]->show();

        anim1[i] = new QPropertyAnimation(toolButtons[i], "geometry");
        anim1[i]->setDuration(250);
        anim1[i]->setStartValue(QRect(buttonx + 20, TOOL_BUTTON_Y, TOOL_BUTTON_WIDTH, TOOL_BUTTON_HEIGHT));

    }


    animGroup = new QParallelAnimationGroup;

    for(int i = 0; i< total_user_count + 1; i++){

        buttonx = framestart + ((defaultframelength - TOOL_BUTTON_WIDTH) / 2) + (i * defaultframelength);
        anim1[i]->setEndValue(QRect(buttonx ,TOOL_BUTTON_Y, TOOL_BUTTON_WIDTH, TOOL_BUTTON_HEIGHT));
        animGroup->addAnimation(anim1[i]);
    }

    animGroup->start();

}


void LoginForm::userButtonClicked(){

    QObject *senderObj = sender(); // This will give Sender object
    QString senderObjName = senderObj->objectName();

    for(int i = 0; i< total_user_count + 1; i++){

        if(senderObjName.compare(QString("toolbutton%1").arg(i)) == 0){
            currentUserIndex = i;
            break;
        }

    }

    loginPageTransition();
}


void LoginForm::loginPageTransition(){


    int middlepoint = ui->usersframe->width() / 2;
    int buttonx = middlepoint - (TOOL_BUTTON_WIDTH / 2);

    animGroup->clear();
    animGroup = new QParallelAnimationGroup;

    for(int i = 0; i < total_user_count + 1; i++){

        anim1[i] = new QPropertyAnimation(toolButtons[i], "geometry");

        anim1[i]->setDuration(150);

        if(i == currentUserIndex){
            anim1[i]->setStartValue(QRect(toolButtons[i]->x(), toolButtons[i]->y(), toolButtons[i]->width(), toolButtons[i]->height()));
            anim1[i]->setEndValue(QRect(buttonx ,TOOL_BUTTON_Y, TOOL_BUTTON_WIDTH, TOOL_BUTTON_HEIGHT));

        }else{

            anim1[i]->setStartValue(QRect(toolButtons[i]->x(), toolButtons[i]->y(), toolButtons[i]->width(), toolButtons[i]->height()));
            anim1[i]->setEndValue(QRect(buttonx ,TOOL_BUTTON_Y, TOOL_BUTTON_WIDTH, TOOL_BUTTON_HEIGHT));

        }

        animGroup->addAnimation(anim1[i]);
    }

    animGroup->start();

    for(int i = 0; i< total_user_count + 1; i++){

        if(i != currentUserIndex){
            toolButtons[i]->setStyleSheet(QString("background-color:rgba(50, 50, 50,0.9);\nfont-size:%1px;\n").arg(TOOL_BUTTON_FONT_SIZE));
        }else{
            toolButtons[i]->setStyleSheet(QString("background-color:rgba(200, 200, 200,0.9);\ncolor:black;\nfont-size:%1px;\nborder:2px solid rgb(255,202,8);").arg(TOOL_BUTTON_FONT_SIZE));
        }
    }

    animationTimer->setTimerType(Qt::TimerType::CoarseTimer);
    animationTimer->setSingleShot(false);
    animationTimer->setInterval(160);
    animationTimerState = 0;
    animationTimer->start();


}

void LoginForm::animationTimerFinished(){

    switch(animationTimerState){
    case 0:

        animGroup->clear();

        for(int i = 0; i< total_user_count + 1; i++){

            if(i != currentUserIndex){
                toolButtons[i]->hide();
            }else{
                toolButtons[i]->setStyleSheet(QString("background-color:rgba(50, 50, 50,0.9);\nfont-size:%1px;\nborder-radius=10px;").arg(TOOL_BUTTON_FONT_SIZE));
            }
            toolButtons[i]->setText("");
        }

        animationTimerState++;
        animationTimer->stop();
        animationTimer->setInterval(260);
        animationTimer->start();


        ui->logolabel->setPixmap(toolButtons[currentUserIndex]->icon().pixmap(ui->logolabel->width(), ui->logolabel->height()));


        anim1[0] = new QPropertyAnimation(toolButtons[currentUserIndex], "geometry");
        anim1[0]->setDuration(250);

        anim1[0]->setStartValue(QRect(toolButtons[currentUserIndex]->x(), toolButtons[currentUserIndex]->y(),
                                      toolButtons[currentUserIndex]->width(), toolButtons[currentUserIndex]->height()));
        /*
        anim1[0]->setEndValue(QRect(ui->loginframe->x() - ui->loginpage->layout()->contentsMargins().left(),
                                    ui->loginframe->y() - ui->loginpage->layout()->contentsMargins().top(),
                                    ui->loginframe->width(), ui->loginframe->height()));
*/

        //todo: I have to get this values correctly
        anim1[0]->setEndValue(QRect(166, 7, 432, 300));

        anim1[0]->start();
        break;

    case 1:

        animationTimerState++;
        animationTimer->stop();
        animationTimer->setInterval(50);
        animationTimer->start();
        break;

    case 2:

        pageTransition(ui->loginpage);
        ui->userspage->setFocus();
        userSelectStateMachine(0, currentUserIndex);

        // ui->passwordInput->setFocus();
        capsLockCheck();
        animationTimerState = 0;
        animationTimer->stop();

        break;

    }

}


void LoginForm::userSelectStateMachine(int key, int button){


    if(key == Qt::Key_Left)
        currentUserIndex--;
    else if(key == Qt::Key_Right)
        currentUserIndex++;

    if(currentUserIndex < 0)
        currentUserIndex = 0;

    if(currentUserIndex >= total_user_count + 1)
        currentUserIndex = total_user_count;

    if(button >= total_user_count + 1)
        button = total_user_count;

    if(button >= 0)
        currentUserIndex  = button;


    for(int i = 0; i< total_user_count + 1; i++){

        toolButtons[i]->setStyleSheet(QString("background-color:rgba(50, 50, 50,0.9);\nfont-size:%1px;\n").arg(TOOL_BUTTON_FONT_SIZE));
    }

    toolButtons[currentUserIndex]->setStyleSheet(QString("background-color:rgba(200, 200, 200,0.9);\ncolor:black;\nfont-size:%1px;\nborder:2px solid rgb(255,202,8);").arg(TOOL_BUTTON_FONT_SIZE));

    ui->usersframe->setFocus();

    if(currentUserIndex < total_user_count){



        ui->userLabel->show();
        ui->userInput->setText(userList[currentUserIndex]);
        ui->userLabel->setText(userList[currentUserIndex]);
        ui->userInput->hide();
        ui->passwordInput->setFocus();
    }else{
        ui->userInput->show();
        ui->userInput->setText("");
        ui->userInput->setFocus();
        ui->userLabel->hide();
    }

}


void LoginForm::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->userspage)){
            loginPageTransition();

        }else if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->resetpage))
            on_resetpasswordButton_clicked();
        else if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->warningpage)){
            on_acceptbutton_clicked();
        }
        else if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->loginpage)){

            if (ui->userInput->text().isEmpty()){
                ui->userInput->setFocus();
            }else if( ui->userInput->hasFocus()){
                ui->passwordInput->setFocus();
            }else{
                startLogin();
            }

        }
    }
    else if (event->key() == Qt::Key_Escape) {

        if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->loginpage)){

            needPasswordChange = 0;
            cancelLogin();

            if(Cache().getLastUser() != NULL){
                pageTransition(ui->userspage);
                userSelectStateMachine(0, currentUserIndex);
            }

        }else if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->resetpage) && !resetStartFlag){
            on_cancelResetButton_clicked();
        }else if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->waitpage) &&
                 loginStartFlag && !resetStartFlag){
            // m_Greeter.cancelAuthentication();
            //pageTransition(ui->loginpage);

        }
    }
    else if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right &&
             ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->userspage)) {
        userSelectStateMachine(event->key(), -1);
    }else {
        QWidget::keyPressEvent(event);
    }

}


void LoginForm::keyReleaseEvent(QKeyEvent *event){

    if(event->key() == Qt::Key_CapsLock){
        QWidget::keyReleaseEvent(event);
        capsLockCheck();
    }else{
        QWidget::keyReleaseEvent(event);
    }

}


void LoginForm::on_resetpasswordButton_clicked()
{
    ui->rstpwdmessagelabel->clear();

    if(ui->oldpasswordinput->text().isEmpty()){
        ui->rstpwdmessagelabel->setText(tr("Old password is wrong"));
        return;
    }

    if(ui->newpasswordinput->text().isEmpty() || ui->newpasswordconfirminput->text().isEmpty()){
        ui->rstpwdmessagelabel->setText(tr("New passwords are not same"));
        return;
    }

    if(ui->oldpasswordinput->text().compare(oldPassword)){

        ui->rstpwdmessagelabel->setText(tr("Old password is wrong"));

    }else if(ui->newpasswordconfirminput->text().compare(ui->newpasswordinput->text())){

        ui->rstpwdmessagelabel->setText(tr("New passwords are not same"));

    }else{
        /* everything is ok */
        resetTimer->setTimerType(Qt::TimerType::CoarseTimer);

        resetTimer->setSingleShot(false);

        if(needPasswordChange == false){
            cancelLogin();
            needPasswordChange = true;
            resetTimerState = 0;
            resetTimer->setInterval(10);
            qInfo() << "Reset password operation started again for " + ui->userInput->text().trimmed();


        }else if(needReenterOldPassword){
            //needReenterOldPassword = 0;
            resetTimerState = 3;
            resetTimer->setInterval(10);
            qInfo() << "Reset password operation started for " + ui->userInput->text().trimmed();
        }else{

            resetTimerState = 4;
            qInfo() <<  "Reset password operation started for " + ui->userInput->text().trimmed();
        }

        resetTimer->start();

        promptFlag = 0;
        ui->newpasswordinput->setEnabled(false);
        ui->newpasswordconfirminput->setEnabled(false);
        ui->oldpasswordinput->setEnabled(false);
    }
}

void LoginForm::LoginTimerFinished(){

    bool endFlag = 0;
    static int promptCheckCounter;
    static int userCheckCounter;


    switch(loginTimerState){

    case 0:

        qInfo() << "Login start for " + ui->userInput->text().trimmed();

        ui->waitlabel->setText(tr("Authenticating"));
        pageTransition(ui->waitpage);
        //cancelLogin();
        userCheckCounter = 0;
        loginTimerState = 1;
        lastPrompt.clear();

        loginTimer->setInterval(100);

        break;

    case 1:
        //enter user

        if(!m_Greeter.isAuthenticated()){
            //cancelLogin();
        }

        qInfo() << "User name is sending";
        m_Greeter.authenticate(ui->userInput->text().trimmed());
        loginTimerState = 2;
        promptCheckCounter = 0;
        lastPrompt.clear();
        loginTimer->setInterval(500);
        break;

    case 2:

        if(loginprompt){
            loginTimer->setInterval(500);
            loginprompt = false;
        }else{

            if(m_Greeter.isAuthenticated()){

                qWarning() << "user already Authenticated";
                m_Greeter.startSessionSync(currentSession());
                endFlag = 1;
                promptCheckCounter = 0;

            }else if((lastPrompt.compare("Password: ") == 0 || lastPrompt.compare("password: ") == 0) && m_Greeter.inAuthentication()){

                qInfo() << "password is sending";
                m_Greeter.respond(ui->passwordInput->text());
                endFlag = 1;
                promptCheckCounter = 0;
            }else if(promptCheckCounter > 40){
                pageTransition(ui->loginpage);
                endFlag = 1;
                ui->passwordInput->setEnabled(true);
                ui->userInput->setEnabled(true);
                ui->passwordInput->setFocus();
                ui->passwordInput->clear();
                cancelLogin();
                messageReceived = 0;
                loginStartFlag = false;

                if(networkOK){
                    ui->messagelabel->setText(tr("Error : User is unknown"));
                }else{
                    ui->messagelabel->setText(tr("Error : Network is not connected"));
                }

                loginTimer->setInterval(100);

                qWarning() << "No password prompt received";
                qWarning() << "Authentication is canceled";

            }else{
                promptCheckCounter++;
                loginTimer->setInterval(100);

            }

        }
        break;
    }

    if(endFlag == 1){
        loginTimerState = 0;
        loginTimer->stop();
    }else{
        loginTimer->stop();

        loginTimer->start();
        loginStartFlag = true;
        promptFlag = false;
    }
}

void LoginForm::passwordResetTimerFinished(){

    bool endFlag = 0;
    static int opcheckcounter;

    switch(resetTimerState){

    case 0:
        ui->resetpasswordButton->setEnabled(false);
        pageTransition(ui->waitpage);
        cancelLogin();
        resetTimerState = 1;
        break;

    case 1:
        //enter user
        m_Greeter.authenticate(ui->userInput->text().trimmed());
        resetTimerState = 2;
        break;

    case 2:
        //enter password
        m_Greeter.respond(oldPassword);
        resetTimerState = 3;
        break;

    case 3:
        //old password
        ui->resetpasswordButton->setEnabled(false);
        pageTransition(ui->waitpage);
        if(lastPrompt.compare("(current) UNIX password: ") == 0 || lastPrompt.compare("Current Password: ") == 0)
            m_Greeter.respond(oldPassword);

        resetTimerState = 4;
        break;

    case 4:
        //"Enter new password: "
        ui->resetpasswordButton->setEnabled(false);
        pageTransition(ui->waitpage);
        m_Greeter.respond(ui->newpasswordinput->text());
        resetTimerState = 5;
        break;

    case 5:
        //"Enter it again: "
        m_Greeter.respond(ui->newpasswordinput->text());
        //resetTimerState = 0;
        resetTimerState = 6;
        opcheckcounter = 0;

        // endFlag = 1;
        break;

    case 6:
        if(opcheckcounter < 6){

            opcheckcounter++;
            resetTimerState = 6;

        }else{
            opcheckcounter = 0;
            endFlag = true;
            resetStartFlag = false;
            loginStartFlag = false;
            //pageTransition(ui->loginpage);
        }
        break;

    }

    if(endFlag == true){

        ui->resetpasswordButton->setEnabled(true);
        resetTimerState = 0;
        resetTimer->stop();
    }else{
        resetTimer->stop();
        resetTimer->setInterval(500);
        resetTimer->start();
        resetStartFlag = true;
    }

}

void LoginForm::on_acceptbutton_clicked()
{
    pageTransition(ui->resetpage);
    ui->newpasswordinput->setFocus();

}


QString LoginForm::getValueOfString(QString data, QString value){

    QString result;

    int indx = data.indexOf(value);
    int endx = 0;

    if(indx == -1){
        return NULL;
    }

    indx += value.size();
    result = data.mid(indx);

    if(result.at(0) != QChar(':') && result.at(0) != QChar('=') ){

        if(result.at(1) != QChar(':') && result.at(1) != QChar('=') )
            return NULL;
        else
            indx = 2;

    }else{
        indx = 1;
    }


    while(result.at(indx) == ' ' || result.at(indx) == '\n'){
        indx++;
    }

    endx = indx;

    while(result.at(endx) != ' ' && result.at(endx) != '\n'){
        endx++;
    }

    result = result.mid(indx, (endx - indx));

    return result;
}


bool LoginForm::capsOn()
{
    FILE *fp;
    char data[128];
    bool ret = false;


    fp = popen("xset q | grep Caps", "r");
    if (fp == NULL) {
        qWarning() << "Unable to open capslock status" ;
        return false;
    }


    int read_size = fread(data, 1, sizeof(data), fp);

    if(read_size < 1){
        qWarning() << " Unable to read capslock status" ;

        /* close */
        pclose(fp);
        return false;
    }

    if(getValueOfString(QString::fromLocal8Bit(data), QString("Caps Lock")).compare("off") == 0)
        ret = false;
    else
        ret = true;

    pclose(fp);
    return ret;

}

void LoginForm::on_userInput_editingFinished()
{
    capsLockCheck();
}


void LoginForm::capsLockCheck(){


    //check caps lock position
    if(capsOn()){
        if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->loginpage))
            ui->messagelabel->setText(tr("Caps Lock is on"));
        else if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->resetpage))
            ui->rstpwdmessagelabel->setText(tr("Caps Lock is on"));
    }else{


        if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->loginpage)){
            if(ui->messagelabel->text().compare(tr("Caps Lock is on")) == 0)
                ui->messagelabel->clear();
        }else if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->resetpage)){
            if(ui->rstpwdmessagelabel->text().compare(tr("Caps Lock is on")) == 0)
                ui->rstpwdmessagelabel->clear();
        }
    }

}

void LoginForm::stopWaitOperation(const bool& networkstatus){


    int tm = Settings().network_ok_timeout();
    if(tm == 0)
        tm = 1;
    else
        tm = tm /5;

    networkOK = networkstatus;

    if(ui->stackedWidget->currentIndex() == ui->stackedWidget->indexOf(ui->waitpage) && !loginStartFlag && !resetStartFlag){

        if(networkstatus == false && loginTimeot < Settings().waittimeout()){
            loginTimeot += 5;
        }else{

            nwcheckcount++;

            if(nwcheckcount > tm){

                nwcheckcount = 0;
                if(networkstatus == false)
                    timeoutFlag = true;
                else
                    timeoutFlag = false;

                if(Cache().getLastUser() != NULL){
                    pageTransition(ui->userspage);
                    ui->userspage->setFocus();
                    ui->userspage->setFocusPolicy(Qt::FocusPolicy::WheelFocus);

                }else{
                    ui->userLabel->hide();

                    pageTransition(ui->loginpage);

                }

                loginTimeot = 0;

            }


        }

    }else if(ui->stackedWidget->currentIndex() != ui->stackedWidget->indexOf(ui->waitpage) &&
             ui->stackedWidget->currentIndex() != ui->stackedWidget->indexOf(ui->userspage) &&
             (networkstatus == false) && Settings().waittimeout() > 0 && !timeoutFlag &&
             !loginStartFlag && !resetStartFlag){

        ui->waitlabel->setText(tr("Waiting for Network and Services"));
        pageTransition(ui->waitpage);
        cancelLogin();
        loginTimeot = 0;
        loginStartFlag = false;
        resetStartFlag = false;

    }

}


void LoginForm::on_loginbutton_clicked()
{
    startLogin();
}


void LoginForm::pageTransition(QWidget *Page){


    ui->stackedWidget->setCurrentIndex(ui->stackedWidget->indexOf(Page));


    if(Page == ui->waitpage){
        ui->waitlabel->setFocus();
        mv->start();
        ui->userInput->clearFocus();
        ui->passwordInput->clearFocus();
#ifdef SCREENKEYBOARD
        emit sendKeyboardCloseRequest();
#endif
    }
    else if(Page == ui->userspage){
        usersbuttonReposition();
        ui->userspage->setFocus();
        ui->userspage->setFocusPolicy(Qt::FocusPolicy::WheelFocus);
        ui->userInput->clearFocus();
        ui->passwordInput->clearFocus();
        ui->passwordInput->setEnabled(false);
        ui->userInput->setEnabled(false);
        mv->stop();
#ifdef SCREENKEYBOARD
        emit sendKeyboardCloseRequest();
#endif
    }
    else if(Page == ui->loginpage){

        if(ui->userInput->text().isEmpty())
            ui->userInput->setFocus();
        else
            ui->passwordInput->setFocus();

        ui->passwordInput->setEnabled(true);
        ui->userInput->setEnabled(true);

        ui->passwordInput->clear();

        if(!loginStartFlag)
            ui->messagelabel->clear();

        mv->stop();
    }
    else if(Page == ui->warningpage){

        ui->userInput->clearFocus();
        ui->passwordInput->clearFocus();
        mv->stop();

#ifdef SCREENKEYBOARD
        emit sendKeyboardCloseRequest();
#endif
    }
    else if(Page == ui->resetpage){
        ui->userInput->clearFocus();
        ui->passwordInput->clearFocus();

        mv->stop();
    }


}


void LoginForm::on_pwShowbutton_pressed()
{
    ui->passwordInput->setEchoMode(QLineEdit::EchoMode::Normal);
    ui->passwordInput->setDisabled(true);
}

void LoginForm::on_pwShowbutton_released()
{
    ui->passwordInput->setEchoMode(QLineEdit::EchoMode::Password);
    ui->passwordInput->setDisabled(false);
    //ui->pwShowbutton->clearFocus();
    ui->passwordInput->setFocus();

}

void LoginForm::on_backButton_clicked()
{
    needPasswordChange = 0;
    cancelLogin();

    if(Cache().getLastUser() != NULL){
        pageTransition(ui->userspage);
        userSelectStateMachine(0, currentUserIndex);
    }
}

void LoginForm::on_cancelResetButton_clicked()
{



    if(!resetStartFlag){
        needPasswordChange = 0;
        cancelLogin();
        pageTransition(ui->loginpage);
        ui->newpasswordconfirminput->clear();
        ui->newpasswordinput->clear();
        ui->oldpasswordinput->clear();
        ui->userInput->setEnabled(true);
        ui->passwordInput->setEnabled(true);
        ui->passwordInput->clear();

        if(ui->userInput->isHidden())
            ui->passwordInput->setFocus();
        else if(!ui->userInput->text().isEmpty())
            ui->passwordInput->setFocus();
        else
            ui->userInput->setFocus();

        capsLockCheck();
        ui->rstpwdmessagelabel->clear();

    }
}

#ifdef SCREENKEYBOARD
void LoginForm::focusChanged(QWidget *old, QWidget *now){



    if(now == NULL){
        //  screenKeyboard->close();
#if 0
        if(screenKeyboard->isHidden()){
            ui->loginframe->setFocus();
        }
#endif
        return;
    }



    if (now->objectName().compare(ui->userInput->objectName()) == 0){


        emit sendKeyboardRequest( ui->userInput->mapToGlobal(QPoint(0,0)), ui->userInput->width());

    }else if(now->objectName().compare(ui->passwordInput ->objectName()) == 0){

        emit sendKeyboardRequest(ui->passwordInput->mapToGlobal(QPoint(0,0)), ui->passwordInput->width());

    }else if(now->objectName().compare(ui->oldpasswordinput ->objectName()) == 0){

        emit sendKeyboardRequest( ui->oldpasswordinput->mapToGlobal(QPoint(0,0)), ui->oldpasswordinput->width());

    }else if(now->objectName().compare(ui->newpasswordinput ->objectName()) == 0){

        emit sendKeyboardRequest( ui->newpasswordinput->mapToGlobal(QPoint(0,0)), ui->newpasswordinput->width());

    }else if(now->objectName().compare(ui->newpasswordconfirminput ->objectName()) == 0){

        emit sendKeyboardRequest( ui->newpasswordconfirminput->mapToGlobal(QPoint(0,0)), ui->newpasswordconfirminput->width());

    }
}

void LoginForm::keyboardEvent(QString key){


    QWidget * fw = qApp->focusWidget();
    QString txt;




    if(key.compare(QString("enter")) == 0){
        QKeyEvent *event = new QKeyEvent(QEvent::KeyPress, Qt::Key_Enter, Qt::NoModifier);
        QCoreApplication::postEvent ((QObject*)ui->passwordInput, event);
        return;
    }


    if(fw == NULL){
        return;
    }


    QLineEdit *target = NULL;


    if(fw->objectName().compare(ui->userInput->objectName()) == 0){

        target = ui->userInput;
    }else if(fw->objectName().compare(ui->passwordInput->objectName()) == 0){

        target = ui->passwordInput;
    }else if(fw->objectName().compare(ui->oldpasswordinput->objectName()) == 0){

        target = ui->oldpasswordinput;

    }else if(fw->objectName().compare(ui->newpasswordinput->objectName()) == 0){

        target = ui->newpasswordinput;

    }else if(fw->objectName().compare(ui->newpasswordconfirminput->objectName()) == 0){

        target = ui->newpasswordconfirminput;

    }


    if(target != NULL){

        QString tmptextleft;
        QString tmptextright;

        int textlen;

        if(key.compare(QString("clear")) == 0){
            txt = "";
            target->setText(txt);
        }else if(key.compare(QString("backspace")) == 0){

            int cursorpos = target->cursorPosition();
            tmptextleft = target->text().mid(0, cursorpos);
            tmptextright = target->text().mid(cursorpos, target->text().length());
            textlen = tmptextleft.length();
            tmptextleft = tmptextleft.mid(0, textlen - 1);
            txt = tmptextleft + tmptextright;
            target->setText(txt);
            target->setCursorPosition(cursorpos - 1);

        }else{

            txt = target->text() + key;
            target->setText(txt);
        }
    }

}


void LoginForm::keyboardCloseEvent(void){

    QWidget * fw = qApp->focusWidget();

    if(fw == NULL){
        return;
    }

    if(fw->objectName().compare(ui->userInput->objectName()) == 0){
        ui->userInput->clearFocus();
    }else if(fw->objectName().compare(ui->passwordInput->objectName()) == 0){
        ui->passwordInput->clearFocus();

    }else if(fw->objectName().compare(ui->oldpasswordinput->objectName()) == 0){
        ui->oldpasswordinput->clearFocus();

    }else if(fw->objectName().compare(ui->newpasswordinput->objectName()) == 0){
        ui->newpasswordinput->clearFocus();

    }else if(fw->objectName().compare(ui->newpasswordconfirminput->objectName()) == 0){
        ui->newpasswordconfirminput->clearFocus();
    }
}

#endif



void LoginForm::resetRequest(){
    qWarning() << "reset requested";
}


void LoginForm::setGreeter(){

}




