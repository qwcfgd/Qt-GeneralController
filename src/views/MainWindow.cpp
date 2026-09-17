#include "MainWindow.h"
#include "ui_MainWindow.h"
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QScreen>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QComboBox>
#include <QPainter>
#include <QScopedValueRollback>
#include <QTabBar>
#include <QFileDialog>
#include <QMenu>
#include <QPointer>
static void initHostResources(){
    static const bool initialized=[](){ Q_INIT_RESOURCE(resources); return true; }();
    Q_UNUSED(initialized);
}
namespace host {
static QIcon statusIcon(int state) {
    QPixmap pix(32,32);pix.setDevicePixelRatio(2);pix.fill(Qt::transparent);
    QPainter p(&pix);p.setRenderHint(QPainter::Antialiasing);
    const QColor color=state==1?QColor("#16A06B"):state==2?QColor("#DC4040"):QColor("#96A1AD");
    p.setPen(QPen(color,1.6));p.setBrush(state?QBrush(color):Qt::NoBrush);
    p.drawEllipse(QRectF(3,3,10,10));return QIcon(pix);
}
MainWindow::MainWindow(const QString &settingsPath,bool simulation,QWidget *parent):
    QMainWindow(parent),ui(new Ui::MainWindow),m_store(settingsPath),m_simulation(simulation) {
    initHostResources();ui->setupUi(this);ui->heroSubtitle->hide();setStyleSheet(styleSheetText());setWindowIcon(QIcon(":/Bootloader.ico"));
    ui->versionBadge->setText(MainWindowInitialValues::versionLabel);
    ui->heroTitle->setText(MainWindowInitialValues::title);ui->eyebrow->setText(MainWindowInitialValues::description);
    ui->channelTabs->setUsesScrollButtons(true);ui->channelTabs->tabBar()->setExpanding(false);
    ui->heroLayout->removeWidget(ui->versionBadge);
    ui->versionBadge->setStyleSheet("color:#708397;background:transparent;padding:2px 8px;font-size:11px;");
    ui->statusbar->addPermanentWidget(ui->versionBadge);
    ui->channelTabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->channelTabs->tabBar(),&QWidget::customContextMenuRequested,this,[this](const QPoint &position){
        const int index=ui->channelTabs->tabBar()->tabAt(position);if(index<0)return;
        ui->channelTabs->setCurrentIndex(index);QPointer<ChannelViewModel> target=m_channels[index];
        QMenu menu(this);menu.setObjectName("channelContextMenu");
        auto remove=menu.addAction("删除通道");remove->setObjectName("deleteChannelAction");
        if(menu.exec(ui->channelTabs->tabBar()->mapToGlobal(position))==remove && target)removeChannel(target);
    });
    auto can=ChannelSettings::defaults(communication::Bus::Can);can.simulation=simulation;
    auto lin=ChannelSettings::defaults(communication::Bus::Lin);lin.simulation=simulation;
    QString error;addChannel(can,error);addChannel(lin,error);ui->channelTabs->setCurrentIndex(0);
    connect(ui->createChannel,&QPushButton::clicked,this,&MainWindow::createChannelDialog);
    connect(ui->loadSettings,&QPushButton::clicked,this,&MainWindow::loadChannels);
    connect(ui->saveSettings,&QPushButton::clicked,this,[this](){
        QVector<ChannelSettings> settings;
        for(auto vm:m_channels){if(vm->busy())return;settings.append(vm->settings());}
        QString error;const bool ok=m_store.save(settings,error);
        ui->statusbar->showMessage(ok?QString("已保存 %1 个软件通道").arg(settings.size()):error,8000);
    });
    ensurePolished();
    if(screen())resize(QSize(MainWindowInitialValues::width,MainWindowInitialValues::height).boundedTo(screen()->availableGeometry().size()-QSize(32,64)));
}
MainWindow::~MainWindow(){delete ui;}
bool MainWindow::removeChannel(ChannelViewModel *vm) {
    const int index=m_channels.indexOf(vm);if(index<0)return false;
    const QString name=vm->settings().softwareId;
    m_updating=true;
    disconnect(vm,nullptr,this,nullptr);
    auto page=ui->channelTabs->widget(index);ui->channelTabs->removeTab(index);m_channels.removeAt(index);
    delete page;
    // The model stops timers/tasks, closes only its own port and joins its worker before the lease is offered again.
    delete vm;m_updating=false;updateChannels();
    ui->statusbar->showMessage("已删除 "+name+(m_channels.isEmpty()?"；点击“创建通道”添加新页":""),5000);
    return true;
}
ChannelViewModel *MainWindow::canChannel()const {
    for(auto vm:m_channels)if(vm->settings().bus==communication::Bus::Can)return vm;return nullptr;
}
ChannelViewModel *MainWindow::linChannel()const {
    for(auto vm:m_channels)if(vm->settings().bus==communication::Bus::Lin)return vm;return nullptr;
}
QString MainWindow::nextChannelName(communication::Bus bus)const {
    int number=1;for(auto vm:m_channels)if(vm->settings().bus==bus)++number;
    const QString prefix=bus==communication::Bus::Can?"CAN":"LIN";
    for(;;++number){
        const auto candidate=prefix+QString::number(number).rightJustified(2,'0');
        bool used=false;for(auto vm:m_channels)if(vm->settings().softwareId.compare(candidate,Qt::CaseInsensitive)==0)used=true;
        if(!used)return candidate;
    }
}
ChannelViewModel *MainWindow::addChannel(ChannelSettings settings,QString &error) {
    error.clear();settings.softwareId=settings.softwareId.trimmed();
    if(m_channels.size()>=64){error="最多支持 64 个软件通道。";return nullptr;}
    for(auto vm:m_channels)if(vm->settings().softwareId.compare(settings.softwareId,Qt::CaseInsensitive)==0){
        error="通道名称已存在，请使用其他名称。";return nullptr;
    }
    communication::SoftwareChannelConfiguration c;if(!settings.toConfiguration(c,error))return nullptr;
    auto vm=new ChannelViewModel(settings,this);m_channels.append(vm);
    auto page=new ChannelPage(vm);ui->channelTabs->addTab(page,statusIcon(0),settings.softwareId);
    connect(vm,&ChannelViewModel::changed,this,&MainWindow::updateChannels);
    ui->channelTabs->setCurrentWidget(page);updateChannels();return vm;
}
void MainWindow::updateChannels() {
    if(m_updating)return;QScopedValueRollback<bool> guard(m_updating,true);
    bool idle=true,unconnected=true;
    for(int i=0;i<m_channels.size();++i){
        auto vm=m_channels[i];QSet<quint32> used;
        for(auto peer:m_channels)if(peer!=vm && peer->settings().bus==vm->settings().bus &&
            peer->settings().simulation==vm->settings().simulation && peer->reservesHardware() && peer->settings().handle)
                used.insert(peer->settings().handle);
        vm->setReservations(used);
        const int state=vm->communicationIndicator();
        ui->channelTabs->setTabText(i,vm->settings().softwareId);
        ui->channelTabs->setTabIcon(i,statusIcon(state));
        ui->channelTabs->setTabToolTip(i,state==1?"通讯正常":state==2?"通讯丢失":"未通讯");
        idle=idle && !vm->busy();unconnected=unconnected && !vm->hardwareLocked();
    }
    ui->saveSettings->setEnabled(idle);ui->loadSettings->setEnabled(unconnected);
    ui->createChannel->setEnabled(m_channels.size()<64);
}
void MainWindow::createChannelDialog() {
    QDialog dialog(this);dialog.setObjectName("createChannelDialog");dialog.setWindowTitle("创建软件通道");dialog.setMinimumWidth(390);
    auto form=new QFormLayout(&dialog);form->setContentsMargins(24,20,24,20);form->setSpacing(14);
    auto type=new QComboBox;type->setObjectName("channelType");type->addItems({"CAN","LIN"});
    auto name=new QLineEdit(nextChannelName(communication::Bus::Can));name->setObjectName("channelName");name->setMaxLength(64);
    auto error=new QLabel;error->setObjectName("inlineError");error->setWordWrap(true);error->hide();
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText("确认");buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    form->addRow("通道类型",type);form->addRow("通道名称",name);form->addRow(error);form->addRow(buttons);
    connect(type,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[&](){
        if(!name->isModified())name->setText(nextChannelName(type->currentIndex()?communication::Bus::Lin:communication::Bus::Can));
    });
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&](){
        auto s=ChannelSettings::defaults(type->currentIndex()?communication::Bus::Lin:communication::Bus::Can);
        s.softwareId=name->text();s.simulation=m_simulation;QString message;
        if(addChannel(s,message))dialog.accept();else {error->setText(message);error->show();}
    });dialog.exec();
}
void MainWindow::loadChannels() {
    for(auto vm:m_channels)if(vm->hardwareLocked())return;
    const QString path=QFileDialog::getOpenFileName(this,"载入已保存通道",QCoreApplication::applicationDirPath()+"/config","JSON (*.json)");
    if(path.isEmpty())return;
    QString error;
    if(!restoreChannels(path,error))ui->statusbar->showMessage(error,8000);
}
bool MainWindow::restoreChannels(const QString &path,QString &error) {
    error.clear();
    for(auto vm:m_channels)if(vm->hardwareLocked()){error="请先断开所有通道并等待操作结束。";return false;}
    QVector<ChannelSettings> settings;
    if(!SettingsStore(path).load(settings,error))return false;
    if(!QFileInfo::exists(path)){error="未找到通道配置。";return false;}
    // Validate the whole file before disposing any page or worker.
    m_updating=true;
    while(ui->channelTabs->count()){auto page=ui->channelTabs->widget(0);ui->channelTabs->removeTab(0);delete page;}
    qDeleteAll(m_channels);m_channels.clear();m_updating=false;
    for(auto s:settings){if(m_simulation)s.simulation=true;addChannel(s,error);}
    ui->channelTabs->setCurrentIndex(0);ui->statusbar->showMessage(QString("已载入 %1 个软件通道").arg(settings.size()),5000);return true;
}
QString MainWindow::styleSheetText(){
    QFile f(":/theme.qss");return f.open(QIODevice::ReadOnly)?QString::fromUtf8(f.readAll()):QString();
}
}
