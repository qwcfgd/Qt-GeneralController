#include "ChannelPage.h"
#include <QBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QProgressBar>
#include <QTableView>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QScrollArea>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QFileDialog>
#include <QInputDialog>
#include <QSignalBlocker>
#include <QTimer>
#include <QStyle>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QScopedValueRollback>
#include <QScreen>
#include "protocol/FlashJob.h"
namespace host {
static QLabel *label(const QString &text,QWidget *parent=nullptr){auto w=new QLabel(text,parent);w->setTextFormat(Qt::PlainText);return w;}
static QFrame *card(const QString &title,QVBoxLayout *&layout) {
    auto frame=new QFrame;frame->setProperty("card",true);
    layout=new QVBoxLayout(frame);layout->setContentsMargins(12,8,12,8);layout->setSpacing(6);
    auto heading=label(title);heading->setObjectName("sectionTitle");layout->addWidget(heading);return frame;
}
static QLineEdit *edit(const char *name,int maxLength=80){auto w=new QLineEdit;w->setObjectName(name);w->setMaxLength(maxLength);return w;}
static QSpinBox *spin(const char *name,int low,int high) {
    auto s=new QSpinBox;s->setObjectName(name);s->setRange(low,high);s->setButtonSymbols(QAbstractSpinBox::PlusMinus);return s;
}
ChannelPage::ChannelPage(ChannelViewModel *vm,QWidget *parent):QWidget(parent),m_vm(vm) {
    setObjectName(vm->settings().bus==communication::Bus::Lin?"linPage":"canPage");
    build();loadSettings();refreshHardware();render();
    connect(vm,&ChannelViewModel::changed,this,&ChannelPage::render);
    connect(vm,&ChannelViewModel::hardwareListChanged,this,&ChannelPage::refreshHardware);
    connect(vm,&ChannelViewModel::settingsChanged,this,[this](){if(!m_applying)loadSettings();});
    connect(vm,&ChannelViewModel::logAdded,m_log,&QPlainTextEdit::appendPlainText);
    connect(vm,&ChannelViewModel::logsCleared,m_log,&QPlainTextEdit::clear);
    auto timer=new QTimer(this);timer->setInterval(ChannelPageInitialValues::elapsedRefreshMs);
    connect(timer,&QTimer::timeout,this,[this](){if(m_vm->taskState()==TaskState::Running && m_elapsed.isValid())
        m_elapsedText->setText(QString("%1 s").arg(m_elapsed.elapsed()/1000.0,0,'f',1));});timer->start();
}
void ChannelPage::build() {
    const bool lin=m_vm->settings().bus==communication::Bus::Lin;
    auto root=new QVBoxLayout(this);root->setContentsMargins(0,6,0,0);root->setSpacing(8);
    auto hardwareCard=new QFrame;hardwareCard->setProperty("card",true);
    auto hardwareLayout=new QVBoxLayout(hardwareCard);hardwareLayout->setContentsMargins(12,6,12,6);hardwareLayout->setSpacing(6);
    auto status=new QHBoxLayout;
    m_state=label("未连接");m_state->setObjectName("connectionState");
    m_busHealth=label("等待设备");m_busHealth->setObjectName("muted");m_busHealth->setMinimumWidth(0);m_busHealth->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    m_device=label("—");m_device->setObjectName("deviceValue");m_handle=label("—");m_handle->setObjectName("handleValue");
    status->addWidget(m_state);status->addWidget(m_busHealth,1);
    status->addWidget(label("设备通道"));status->addWidget(m_device);status->addSpacing(14);status->addWidget(label("硬件通道"));status->addWidget(m_handle);
    hardwareLayout->addLayout(status);
    auto row=new QHBoxLayout;row->setSpacing(8);
    m_parameters=new QWidget;auto grid=new QGridLayout(m_parameters);grid->setContentsMargins(0,0,0,0);grid->setHorizontalSpacing(8);grid->setVerticalSpacing(3);
    m_mode=new QComboBox;m_mode->setObjectName("modeCombo");m_mode->addItems({ChannelPageInitialValues::onlineLabel,ChannelPageInitialValues::simulationLabel});
    m_hardware=new QComboBox;m_hardware->setObjectName("hardwareCombo");
    m_hardware->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);m_hardware->setMinimumContentsLength(12);
    m_software=new QComboBox;m_software->setObjectName("softwareChannelCombo");
    m_software->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);m_software->setMinimumContentsLength(8);
    m_software->setToolTip("将本软件通道绑定到所选设备的可用硬件通道；其他页面已占用的通道不再列出。");
    m_bitrate=new QComboBox;m_bitrate->setObjectName("bitrateCombo");
    const QList<int> rates=lin?QList<int>{19200,9600,10400,20000,2400,4800}:QList<int>{1000000,800000,500000,250000,125000,100000,50000,20000,10000,5000};
    for(int rate:rates)m_bitrate->addItem(QString::number(rate)+" bit/s",rate);
    QStringList names={"硬件选择","软件通道","波特率","运行模式"};
    QList<QComboBox*> combos={m_hardware,m_software,m_bitrate,m_mode};
    for(int i=0;i<4;++i){grid->addWidget(label(names[i]),0,i);grid->addWidget(combos[i],1,i);}
    grid->setColumnStretch(0,3);grid->setColumnStretch(1,2);
    row->addWidget(m_parameters,1);
    auto actions=new QVBoxLayout;actions->setSpacing(4);
    m_reconnect=new QCheckBox("自动重连");m_reconnect->setObjectName("autoReconnect");
    m_reconnect->setToolTip("仅在硬件提供可靠持续身份时启用；手动断开会停止重连。");
    m_protocol=new QPushButton("通道设置…");m_protocol->setObjectName("protocolSettings");
    status->addSpacing(12);status->addWidget(m_reconnect);status->addWidget(m_protocol);actions->addStretch();
    auto connection=new QHBoxLayout;
    m_refresh=new QPushButton("刷新");m_refresh->setObjectName("refreshButton");
    m_connect=new QPushButton("连接");m_connect->setObjectName("connectButton");m_connect->setProperty("primary",true);
    connection->addWidget(m_refresh);connection->addWidget(m_connect);actions->addLayout(connection);
    row->addLayout(actions);hardwareLayout->addLayout(row);root->addWidget(hardwareCard);

    auto download=new QFrame;download->setObjectName("downloadPanel");download->setProperty("card",true);download->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Minimum);
    auto downloadLayout=new QHBoxLayout(download);downloadLayout->setContentsMargins(12,8,12,8);downloadLayout->setSpacing(16);
    m_images=new QWidget;m_images->setObjectName("imageRegion");
    auto imageGrid=new QGridLayout(m_images);imageGrid->setContentsMargins(0,0,0,0);imageGrid->setHorizontalSpacing(6);imageGrid->setVerticalSpacing(3);
    auto heading=label("下载镜像");heading->setObjectName("sectionTitle");
    m_downloadSettings=new QPushButton("下载设置…");m_downloadSettings->setObjectName("downloadSettings");
    m_downloadSettings->setStyleSheet("QPushButton { padding:2px 8px;min-height:16px;font-size:11px; }");
    auto imageHeader=new QHBoxLayout;imageHeader->addWidget(heading);imageHeader->addStretch();
    imageHeader->addWidget(m_downloadSettings);imageGrid->addLayout(imageHeader,0,0,1,3);
    connect(m_downloadSettings,&QPushButton::clicked,this,&ChannelPage::editDownload);
    m_flashPath=edit("flashPath",32767);m_flashPath->setPlaceholderText("Flash Driver · BIN / HEX（可选）");
    m_appPath=edit("applicationPath",32767);m_appPath->setPlaceholderText("Application · BIN / HEX");
    m_flashInfo=label("");m_appInfo=label("");m_flashInfo->setObjectName("muted");m_appInfo->setObjectName("muted");
    for(auto info:{m_flashInfo,m_appInfo})info->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Preferred);
    for(int i=0;i<2;++i){
        imageGrid->addWidget(label(i==0?"Driver":"Application"),1+i*2,0);
        imageGrid->addWidget(i==0?m_flashPath:m_appPath,1+i*2,1);
        auto file=new QPushButton("文件…");file->setObjectName(i==0?"browseFlash":"browseApplication");
        if(i==0)m_browseFlash=file;
        imageGrid->addWidget(file,1+i*2,2);
        imageGrid->addWidget(i==0?m_flashInfo:m_appInfo,2+i*2,1,1,2);
        connect(file,&QPushButton::clicked,this,[this,i](){browseImage(i==0);});
    }
    m_flashAddress=edit("flashAddress",10);m_appAddress=edit("applicationAddress",10);
    // Image addresses live in the settings dialog; image selection and task controls stay visible.
    for(auto address:{m_flashAddress,m_appAddress}){address->setParent(this);address->hide();}
    imageGrid->setColumnStretch(1,1);
    downloadLayout->addWidget(m_images,3);
    auto divider=new QFrame;divider->setFrameShape(QFrame::VLine);divider->setStyleSheet("color:#E3EAF1");downloadLayout->addWidget(divider);
    auto taskRegion=new QWidget;taskRegion->setObjectName("taskRegion");
    auto runLayout=new QVBoxLayout(taskRegion);runLayout->setContentsMargins(0,0,0,0);runLayout->setSpacing(7);
    auto taskHeading=new QHBoxLayout;auto title=label("下载任务");title->setObjectName("sectionTitle");taskHeading->addWidget(title,1);
    m_elapsedText=label("0.0 s");m_elapsedText->setObjectName("muted");taskHeading->addWidget(m_elapsedText);runLayout->addLayout(taskHeading);
    m_task=label("等待开始");m_task->setWordWrap(true);m_task->setObjectName("taskText");runLayout->addWidget(m_task);
    m_progress=new QProgressBar;m_progress->setObjectName("downloadProgress");m_progress->setRange(0,100);m_progress->setValue(0);m_progress->setMinimumHeight(22);runLayout->addWidget(m_progress);
    auto progressRow=new QHBoxLayout;
    m_start=new QPushButton("开始下载");m_start->setObjectName("startButton");m_start->setProperty("primary",true);
    m_cancel=new QPushButton("取消");m_cancel->setObjectName("cancelButton");progressRow->addWidget(m_start,1);progressRow->addWidget(m_cancel);runLayout->addLayout(progressRow);
    m_hint=label("");m_hint->setObjectName("muted");m_hint->setWordWrap(true);runLayout->addWidget(m_hint);
    m_error=label("");m_error->setObjectName("inlineError");m_error->setWordWrap(true);m_error->hide();runLayout->addWidget(m_error);runLayout->addStretch();
    downloadLayout->addWidget(taskRegion,2);root->addWidget(download);

    auto outputs=new QSplitter(Qt::Horizontal);outputs->setChildrenCollapsible(false);outputs->setMinimumHeight(142);
    QVBoxLayout *frameLayout,*logLayout;auto frameCard=card("报文监视",frameLayout);auto logCard=card("运行日志",logLayout);
    auto frameActions=new QHBoxLayout;auto filter=edit("frameFilter",120);filter->setPlaceholderText("筛选 ID、方向、数据或状态");
    m_count=label("0 条");m_count->setObjectName("muted");
    auto clear=new QPushButton("清空");clear->setObjectName("clearFrames");
    auto exportButton=new QPushButton("导出");m_follow=new QCheckBox("跟随");m_follow->setChecked(ChannelPageInitialValues::followFrames);
    m_rxdEnabled=new QCheckBox("RxD使能");m_rxdEnabled->setObjectName("rxdEnabled");m_rxdEnabled->setVisible(lin);
    m_rxdEnabled->setToolTip("勾选后显示发送帧在 LIN 硬件接收队列中的 0x3C 回读；不勾选仅隐藏该回读，不影响发送确认。");
    m_scan=new QPushButton("扫描帧头");m_scan->setObjectName("scanHeaders");m_scan->setVisible(lin);
    frameActions->addWidget(filter,1);frameActions->addWidget(m_count);frameActions->addWidget(m_rxdEnabled);frameActions->addWidget(m_follow);
    frameActions->addWidget(m_scan);frameActions->addWidget(clear);frameActions->addWidget(exportButton);frameLayout->addLayout(frameActions);
    m_table=new QTableView;m_table->setObjectName("frameTable");m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();m_table->verticalHeader()->setDefaultSectionSize(27);
    auto proxy=new QSortFilterProxyModel(this);proxy->setSourceModel(m_vm->frames());proxy->setFilterKeyColumn(-1);proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_table->setModel(proxy);m_table->setShowGrid(false);m_table->horizontalHeader()->setStretchLastSection(true);
    const QList<int> widths={100,105,90,70,90,46,205,120};for(int i=0;i<widths.size();++i)m_table->setColumnWidth(i,widths[i]);
    frameLayout->addWidget(m_table,1);
    auto logActions=new QHBoxLayout;auto clearLog=new QPushButton("清空");auto exportLog=new QPushButton("导出");
    logActions->addWidget(label(QString("最近 %1 条事件").arg(ChannelPageInitialValues::logCapacity)),1);logActions->addWidget(clearLog);logActions->addWidget(exportLog);logLayout->addLayout(logActions);
    m_log=new QPlainTextEdit;m_log->setObjectName("eventLog");m_log->setReadOnly(true);m_log->setMaximumBlockCount(ChannelPageInitialValues::logCapacity);
    m_log->setPlaceholderText("连接变化、扫描和任务结果将在这里显示。");logLayout->addWidget(m_log);
    frameCard->setMinimumWidth(510);logCard->setMinimumWidth(210);outputs->addWidget(frameCard);outputs->addWidget(logCard);outputs->setSizes({850,330});
    root->addWidget(outputs,1);
    connect(m_connect,&QPushButton::clicked,m_vm,&ChannelViewModel::toggleConnection);
    connect(m_refresh,&QPushButton::clicked,m_vm,&ChannelViewModel::refresh);
    connect(m_start,&QPushButton::clicked,m_vm,&ChannelViewModel::start);
    connect(m_cancel,&QPushButton::clicked,m_vm,&ChannelViewModel::cancel);
    connect(m_scan,&QPushButton::clicked,m_vm,&ChannelViewModel::scanHeaders);
    connect(m_protocol,&QPushButton::clicked,this,&ChannelPage::editProtocol);
    connect(filter,&QLineEdit::textChanged,proxy,&QSortFilterProxyModel::setFilterFixedString);
    connect(clear,&QPushButton::clicked,m_vm->frames(),&FrameTableModel::clear);
    connect(exportButton,&QPushButton::clicked,this,&ChannelPage::exportFrames);
    connect(clearLog,&QPushButton::clicked,m_vm,&ChannelViewModel::clearLogs);
    connect(exportLog,&QPushButton::clicked,this,&ChannelPage::exportLogs);
    const auto count=[this,proxy](){m_count->setText(QString("%1 / %2 条").arg(proxy->rowCount()).arg(m_vm->frames()->rowCount()));};
    connect(proxy,&QAbstractItemModel::rowsInserted,this,[this,count](){count();if(m_follow->isChecked())m_table->scrollToBottom();});
    connect(proxy,&QAbstractItemModel::rowsRemoved,this,count);connect(proxy,&QAbstractItemModel::modelReset,this,count);
    for(auto e:{m_flashPath,m_appPath,m_flashAddress,m_appAddress})
        connect(e,&QLineEdit::textChanged,this,&ChannelPage::applyForm);
    for(auto c:{m_reconnect,m_rxdEnabled})connect(c,&QCheckBox::toggled,this,&ChannelPage::applyForm);
    for(auto c:{m_mode,m_software,m_bitrate})connect(c,qOverload<int>(&QComboBox::currentIndexChanged),this,&ChannelPage::applyForm);
    connect(m_hardware,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](){if(!m_loading && !m_refreshing)refreshPorts(true);});
}
void ChannelPage::loadSettings() {
    m_loading=true;const auto &s=m_vm->settings();
    m_mode->setCurrentIndex(s.simulation?1:0);
    int rate=m_bitrate->findData(s.bitrate);if(rate<0){m_bitrate->addItem(QString::number(s.bitrate)+" bit/s",s.bitrate);rate=m_bitrate->count()-1;}m_bitrate->setCurrentIndex(rate);
    m_reconnect->setChecked(s.autoReconnect);m_rxdEnabled->setChecked(s.rxdEnabled);
    m_flashPath->setText(s.flashPath);m_appPath->setText(s.applicationPath);m_flashAddress->setText(s.flashAddress);m_appAddress->setText(s.applicationAddress);
    m_loading=false;refreshHardware();
}
void ChannelPage::applyForm() {
    if(m_loading || m_applying || m_vm->busy())return;
    auto s=m_vm->settings();s.simulation=m_mode->currentIndex()==1;
    s.bitrate=m_bitrate->currentData().toInt();s.hardwareKey=m_software->currentData().toString();
    s.handle=m_software->currentData(Qt::UserRole+1).toUInt();
    s.autoReconnect=m_reconnect->isChecked();s.rxdEnabled=m_rxdEnabled->isChecked();
    s.flashPath=m_flashPath->text();s.applicationPath=m_appPath->text();s.flashAddress=m_flashAddress->text();s.applicationAddress=m_appAddress->text();
    QScopedValueRollback<bool> guard(m_applying,true);m_vm->setSettings(s);
}
void ChannelPage::refreshHardware() {
    if(m_refreshing)return;QScopedValueRollback<bool> guard(m_refreshing,true);
    QSignalBlocker blocker(m_hardware);const auto &s=m_vm->settings();
    const QString prior=m_hardware->currentData().toString();
    QString selected;QSet<QString> devices;
    m_hardware->clear();
    for(const auto &h:m_vm->hardware()){
        const auto device=hardwareDeviceKey(h);
        if(!devices.contains(device)){
            devices.insert(device);m_hardware->addItem(h.label.section(" / SDK channel",0,0).section(" / channel",0,0),device);
            m_hardware->setItemData(m_hardware->count()-1,h.label+" · "+device,Qt::ToolTipRole);
        }
        if(h.key==s.hardwareKey)selected=device;
    }
    if(selected.isEmpty() && !s.hardwareKey.isEmpty()) {
        // Keep an unplugged selection visible; never silently bind a different adapter.
        selected=s.hardwareKey.section(':',0,-2);
        if(!devices.contains(selected))m_hardware->addItem("已选设备未连接",selected);
    }
    if(selected.isEmpty() && devices.contains(prior))selected=prior;
    if(selected.isEmpty() && devices.size()==1)selected=m_hardware->itemData(0).toString();
    if(selected.isEmpty()){m_hardware->insertItem(0,devices.isEmpty()?"未发现硬件":"请选择硬件",QString());m_hardware->setCurrentIndex(0);}
    else m_hardware->setCurrentIndex(m_hardware->findData(selected));
    m_hardware->setToolTip(m_hardware->currentText());refreshPorts();
}
void ChannelPage::refreshPorts(bool deviceChanged) {
    QSignalBlocker blocker(m_software);const auto s=m_vm->settings();
    const QString device=m_hardware->currentData().toString();int chosen=-1;
    m_software->clear();
    for(const auto &h:m_vm->hardware()) {
        if(hardwareDeviceKey(h)!=device)continue;
        const bool own=(m_vm->hardwareLocked() || m_vm->communicationIndicator()==2) && h.key==s.hardwareKey;
        if(!h.available && !own)continue;
        m_software->addItem(QString("通道 %1").arg(h.controller)+(own && !h.available && !m_vm->connected()?" · 已占用":""),h.key);
        const int row=m_software->count()-1;m_software->setItemData(row,h.handle,Qt::UserRole+1);
        m_software->setItemData(row,QString("SDK 通道 %1 · 句柄 0x%2").arg(h.controller).arg(h.handle,0,16),Qt::ToolTipRole);
        if(!deviceChanged && h.key==s.hardwareKey)chosen=row;
    }
    if(!deviceChanged && chosen<0 && !s.hardwareKey.isEmpty() && (m_vm->hardwareLocked() || m_vm->communicationIndicator()==2)){
        m_software->addItem("原通道 · 未连接",s.hardwareKey);chosen=m_software->count()-1;
        m_software->setItemData(chosen,s.handle,Qt::UserRole+1);
    }
    if(!m_software->count()){m_software->addItem(device.isEmpty()?"请先选择硬件":"无剩余可用通道",QString());chosen=0;}
    m_software->setCurrentIndex(chosen<0?0:chosen);
    if(!m_vm->hardwareLocked() && (deviceChanged || m_vm->communicationIndicator()!=2))applyForm();
    render();
}
void ChannelPage::editProtocol() {
    if(m_vm->hardwareLocked())return;
    const auto original=m_vm->settings();const bool lin=original.bus==communication::Bus::Lin;
    QDialog dialog(this);dialog.setObjectName("protocolDialog");dialog.setWindowTitle(original.softwareId+" · UDS 配置");dialog.setMinimumWidth(520);
    auto layout=new QVBoxLayout(&dialog);layout->setContentsMargins(20,16,20,16);layout->setSpacing(10);
    auto form=new QGridLayout;form->setHorizontalSpacing(10);form->setVerticalSpacing(8);layout->addLayout(form);
    auto profile=edit("profileId");profile->setText(original.profileId);form->addWidget(label("配置名称"),0,0);form->addWidget(profile,0,1,1,3);
    auto request=edit("requestId",10),response=edit("responseId",10),functional=edit("functionalId",10),nad=edit("nad",4);
    request->setText(original.requestId);response->setText(original.responseId);functional->setText(original.functionalId);nad->setText(original.nad);
    auto extended=new QCheckBox("29 位扩展 ID");extended->setObjectName("extendedId");extended->setChecked(original.extendedId);
    // Every widget has an owner, including fields irrelevant to this bus.
    for(auto widget:{request,response,functional,nad})widget->setParent(&dialog);
    extended->setParent(&dialog);
    if(lin){
        form->addWidget(label("NAD · hex"),1,0);form->addWidget(nad,1,1);
        form->addWidget(label("Master · 0x3C / 0x3D"),1,2,1,2);request->hide();response->hide();functional->hide();extended->hide();
    }else{
        form->addWidget(label("请求 ID · hex"),1,0);form->addWidget(request,1,1);form->addWidget(label("响应 ID · hex"),1,2);form->addWidget(response,1,3);
        form->addWidget(label("功能 ID · hex"),2,0);form->addWidget(functional,2,1);form->addWidget(extended,2,2,1,2);nad->hide();
    }
    auto p2=spin("p2Ms",1,60000),star=spin("p2StarMs",1,600000),pending=spin("maxPendingMs",1,3600000);
    auto session=spin("programmingSession",1,127),security=spin("securityLevel",1,125);
    session->setDisplayIntegerBase(16);session->setPrefix("0x");security->setDisplayIntegerBase(16);security->setPrefix("0x");security->setSingleStep(2);
    p2->setValue(original.p2Ms);star->setValue(original.p2StarMs);pending->setValue(original.maxPendingMs);session->setValue(original.programmingSession);security->setValue(original.securityLevel);
    QList<QSpinBox*> values={p2,star,session,security};QStringList titles={"P2 / ms","P2* / ms","编程会话","安全级别"};
    for(int i=0;i<values.size();++i){form->addWidget(label(titles[i]),3+i/2,(i%2)*2);form->addWidget(values[i],3+i/2,(i%2)*2+1);}
    form->addWidget(label("待响应上限 / ms"),5,0);form->addWidget(pending,5,1);
    auto enabled=new QCheckBox("使能 TesterPresent 保活");enabled->setObjectName("testerPresentEnabled");enabled->setChecked(original.testerPresentEnabled);
    auto period=spin("testerPresentMs",1,600000);period->setValue(qMax(1,original.testerPresentMs));period->setEnabled(enabled->isChecked());
    form->addWidget(enabled,6,0,1,2);form->addWidget(label("周期 / ms"),6,2);form->addWidget(period,6,3);
    connect(enabled,&QCheckBox::toggled,period,&QSpinBox::setEnabled);
    auto note=label("参数独立保存到当前软件通道。APP / Boot 流程编程会话固定为 10 02；安全级别和保活按本页配置。");note->setObjectName("muted");note->setWordWrap(true);layout->addWidget(note);
    auto error=label("");error->setObjectName("inlineError");error->setWordWrap(true);error->hide();layout->addWidget(error);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText("保存");buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&](){
        auto s=original;s.profileId=profile->text();s.requestId=request->text();s.responseId=response->text();s.functionalId=functional->text();s.nad=nad->text();s.extendedId=extended->isChecked();
        s.p2Ms=p2->value();s.p2StarMs=star->value();s.maxPendingMs=pending->value();s.programmingSession=session->value();s.securityLevel=security->value();
        s.testerPresentEnabled=enabled->isChecked();s.testerPresentMs=period->value();
        communication::SoftwareChannelConfiguration c;QString message;
        if(!s.toConfiguration(c,message)){error->setText(message);error->show();return;}
        if(m_vm->setSettings(s))dialog.accept();
    });dialog.exec();
}
void ChannelPage::editDownload() {
    if(m_vm->busy())return;
    const auto original=m_vm->settings();boot::FlashProfile profile;QString message;
    if(!boot::FlashProfile::fromJson(original.downloadProfile,profile,message))return;
    QDialog dialog(this);dialog.setObjectName("downloadSettingsDialog");dialog.setWindowTitle(original.softwareId+" · 下载设置");
    dialog.resize(990,520);if(screen())dialog.resize(dialog.size().boundedTo(screen()->availableGeometry().size()-QSize(40,80)));
    auto outer=new QVBoxLayout(&dialog);auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    auto content=new QWidget;auto form=new QFormLayout(content);form->setContentsMargins(12,10,12,10);form->setSpacing(10);
    scroll->setWidget(content);outer->addWidget(scroll,1);
    auto flow=new QComboBox;flow->setObjectName("downloadFlow");flow->addItem("App下载流程","app");flow->addItem("Boot下载流程","boot");
    flow->setCurrentIndex(profile.flow=="boot"?1:0);
    auto flashRequired=new QCheckBox("Flash Driver使能");flashRequired->setObjectName("flashRequired");
    flashRequired->setChecked(original.flashRequired);flashRequired->setToolTip("勾选后下载需要 Flash Driver 镜像。");
    auto downloadRow=new QWidget;auto downloadRowLayout=new QHBoxLayout(downloadRow);downloadRowLayout->setContentsMargins(0,0,0,0);
    downloadRowLayout->addWidget(label("下载流程"));downloadRowLayout->addWidget(flow,1);
    downloadRowLayout->addSpacing(24);downloadRowLayout->addWidget(label("下载镜像"));downloadRowLayout->addWidget(flashRequired);
    form->addRow(downloadRow);
    auto checksWidget=new QWidget;auto grid=new QGridLayout(checksWidget);grid->setContentsMargins(0,0,0,0);
    QVector<QCheckBox*> stepEnables,negativeChecks,timeoutChecks;
    checksWidget->setStyleSheet("QCheckBox::indicator:disabled { background-color:#D9DDE3;border:1px solid #AAB2BE;border-radius:2px; } QCheckBox:disabled { color:#8993A2; }");
    for(int group=0;group<2;++group){
        auto stepHeader=label("步骤"),enableHeader=label("使能"),negativeHeader=label("负响应"),timeoutHeader=label("超时");
        for(auto header:{stepHeader,enableHeader,negativeHeader,timeoutHeader})header->setObjectName("feedbackHeader");
        const int base=group*5;
        grid->addWidget(stepHeader,0,base);grid->addWidget(enableHeader,0,base+1,Qt::AlignHCenter);
        grid->addWidget(negativeHeader,0,base+2,Qt::AlignHCenter);grid->addWidget(timeoutHeader,0,base+3,Qt::AlignHCenter);
        if(group==0)grid->setColumnMinimumWidth(4,24);
    }
    const int rowsPerColumn=(boot::downloadSteps().size()+1)/2;
    for(int i=0;i<boot::downloadSteps().size();++i){
        const auto &step=boot::downloadSteps()[i];
        const auto id=QString::fromLatin1(step.id);
        const int group=i/rowsPerColumn,base=group*5,row=i%rowsPerColumn+1;
        auto stepLabel=label(QString("%1 · %2").arg(i+1,2,10,QChar('0')).arg(QString::fromUtf8(step.label)));
        stepLabel->setObjectName("step_"+id);
        auto stepEnable=new QCheckBox;stepEnable->setObjectName("enable_"+id);
        stepEnable->setChecked(profile.stepEnabled.value(step.id).toBool(profile.flow!="boot"||!step.appOnly));
        stepEnable->setToolTip("勾选：下载时执行该步骤。未勾选：跳过该步骤，负响应和超时判定不可用。");
        auto negative=new QCheckBox;negative->setObjectName("negative_"+id);
        negative->setChecked(profile.negativeResponseChecks.value(step.id).toBool(ChannelPageInitialValues::feedbackChecked));
        negative->setToolTip("勾选：收到除 78 以外的负响应时停止。未勾选：忽略该负响应并继续流程。");
        auto timeout=new QCheckBox;timeout->setObjectName("timeout_"+id);
        timeout->setChecked(!boot::suppressesPositiveResponse(id)&&profile.timeoutChecks.value(step.id).toBool(ChannelPageInitialValues::feedbackChecked));
        timeout->setToolTip(boot::suppressesPositiveResponse(id)?"本步骤开启正响应抑制，超时判定不可用。":"勾选：达到 P2/P2* 或总时限时停止。未勾选：达到时限后忽略该超时并继续下一步。");
        grid->addWidget(stepLabel,row,base);grid->addWidget(stepEnable,row,base+1,Qt::AlignHCenter);
        grid->addWidget(negative,row,base+2,Qt::AlignHCenter);grid->addWidget(timeout,row,base+3,Qt::AlignHCenter);
        stepEnables.append(stepEnable);negativeChecks.append(negative);timeoutChecks.append(timeout);
    }
    grid->setColumnStretch(0,1);grid->setColumnStretch(5,1);
    const auto driverStep=[](int i){
        const auto id=QString::fromLatin1(boot::downloadSteps()[i].id);
        return id=="driver34"||id=="driver36"||id=="driver37"||id=="driverVerify";
    };
    auto enableChecks=[flashRequired,stepEnables,negativeChecks,timeoutChecks,driverStep]{
        for(int i=0;i<negativeChecks.size();++i){
            const bool driverAllowed=flashRequired->isChecked()||!driverStep(i);
            if(!driverAllowed){QSignalBlocker blocker(stepEnables[i]);stepEnables[i]->setChecked(false);}
            stepEnables[i]->setEnabled(driverAllowed);
            const bool active=driverAllowed&&stepEnables[i]->isChecked();
            negativeChecks[i]->setEnabled(active);
            const bool suppressed=boot::suppressesPositiveResponse(QString::fromLatin1(boot::downloadSteps()[i].id));
            if(suppressed){QSignalBlocker blocker(timeoutChecks[i]);timeoutChecks[i]->setChecked(false);}
            timeoutChecks[i]->setEnabled(active&&!suppressed);
        }
    };
    for(auto stepEnable:stepEnables)connect(stepEnable,&QCheckBox::toggled,&dialog,[enableChecks](bool){enableChecks();});
    connect(flow,qOverload<int>(&QComboBox::currentIndexChanged),&dialog,[flow,stepEnables,enableChecks](int){
        for(int i=0;i<stepEnables.size();++i)
            stepEnables[i]->setChecked(flow->currentData()=="app"||!boot::downloadSteps()[i].appOnly);
        enableChecks();
    });
    connect(flashRequired,&QCheckBox::toggled,&dialog,[stepEnables,driverStep,enableChecks](bool checked){
        if(checked)for(int i=0;i<stepEnables.size();++i)if(driverStep(i)){
            stepEnables[i]->setEnabled(true);stepEnables[i]->setChecked(true);
        }
        enableChecks();
    });enableChecks();
    auto feedbackTitle=label("反馈判定");feedbackTitle->setObjectName("feedbackDecisionTitle");
    form->addRow(feedbackTitle);form->addRow(checksWidget);
    auto policy=label("使能：勾选后执行该步骤；未勾选时跳过，且负响应/超时判定不可操作。负响应或超时判定勾选时遇到对应异常停止，未勾选时忽略并继续。App 默认全部使能；Boot 默认不使能 10 02 之前的步骤。下载开始后本页不可修改。");policy->setWordWrap(true);policy->setObjectName("muted");form->addRow(policy);
    auto enabled=new QCheckBox("重复下载使能");enabled->setObjectName("repeatDownloadEnabled");enabled->setChecked(original.repeatDownloadEnabled);
    auto count=spin("repeatDownloadCount",1,10000);count->setValue(original.repeatDownloadCount);
    auto interval=spin("repeatDownloadIntervalMs",0,86400000);interval->setValue(original.repeatDownloadIntervalMs);interval->setSuffix(" ms");
    count->setEnabled(enabled->isChecked());interval->setEnabled(enabled->isChecked());
    connect(enabled,&QCheckBox::toggled,count,&QSpinBox::setEnabled);connect(enabled,&QCheckBox::toggled,interval,&QSpinBox::setEnabled);
    form->addRow(enabled);form->addRow("重复下载次数（含首次）",count);form->addRow("完成后等待间隔",interval);
    auto flashAddress=edit("settingsFlashAddress",10),appAddress=edit("settingsApplicationAddress",10);
    flashAddress->setText(original.flashAddress);appAddress->setText(original.applicationAddress);
    form->addRow("Driver 基址 · hex",flashAddress);form->addRow("App 基址 · hex",appAddress);
    auto keyLibrary=edit("keyLibrary",32767);keyLibrary->setText(profile.keyLibrary);
    form->addRow("27 DLL",keyLibrary);
    auto erase=spin("eraseRoutine",0,65535),verifyRid=spin("verifyRoutine",0,65535),dependencyRid=spin("dependencyRoutine",0,65535),did=spin("identityDid",0,65535);
    for(auto value:{erase,verifyRid,dependencyRid,did}){value->setDisplayIntegerBase(16);value->setPrefix("0x");}
    erase->setValue(profile.eraseRoutine);verifyRid->setValue(profile.verifyRoutine);dependencyRid->setValue(profile.dependencyRoutine);did->setValue(profile.identityDid);
    form->addRow("擦除例程 RID",erase);form->addRow("校验例程 RID",verifyRid);form->addRow("依赖例程 RID",dependencyRid);form->addRow("身份 DID",did);
    auto block=spin("consecutiveFrameByteLimit",3,4095),resetWait=spin("resetWaitMs",0,60000);
    block->setValue(profile.consecutiveFrameByteLimit);block->setSuffix(" Byte");resetWait->setValue(profile.resetWaitMs);resetWait->setSuffix(" ms");
    form->addRow("连续帧字节上限",block);form->addRow("复位后等待",resetWait);
    auto error=label("");error->setObjectName("inlineError");error->setWordWrap(true);error->hide();outer->addWidget(error);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);outer->addWidget(buttons);
    buttons->button(QDialogButtonBox::Ok)->setText("保存");buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&](){
        auto s=m_vm->settings();s.flashRequired=flashRequired->isChecked();
        s.repeatDownloadEnabled=enabled->isChecked();s.repeatDownloadCount=count->value();s.repeatDownloadIntervalMs=interval->value();
        s.flashAddress=flashAddress->text();s.applicationAddress=appAddress->text();
        profile.flow=flow->currentData().toString();profile.stepEnabled={};profile.negativeResponseChecks={};profile.timeoutChecks={};
        for(int i=0;i<stepEnables.size();++i){
            profile.stepEnabled[boot::downloadSteps()[i].id]=stepEnables[i]->isChecked();
            profile.negativeResponseChecks[boot::downloadSteps()[i].id]=negativeChecks[i]->isChecked();
            profile.timeoutChecks[boot::downloadSteps()[i].id]=timeoutChecks[i]->isChecked();
        }
        profile.keyProvider="external-generatekeyex";profile.keyLibrary=keyLibrary->text();profile.simulationOnly=false;
        profile.eraseRoutine=quint16(erase->value());profile.verifyRoutine=quint16(verifyRid->value());profile.dependencyRoutine=quint16(dependencyRid->value());profile.identityDid=quint16(did->value());
        profile.consecutiveFrameByteLimit=block->value();profile.resetWaitMs=resetWait->value();s.downloadProfile=profile.toJson();
        communication::SoftwareChannelConfiguration c;QString message;
        if(!s.toConfiguration(c,message)){error->setText(message);error->show();return;}
        if(m_vm->setSettings(s))dialog.accept();
        else {error->setText("当前通道忙，请在任务结束后修改。");error->show();}
    });dialog.exec();
}
void ChannelPage::render() {
    const auto &s=m_vm->settings();m_state->setText(connectionText(m_vm->state()));
    const bool connected=m_vm->connected();
    m_state->setStyleSheet(connected?"QLabel { color:#087F67;background:#DDF5EA;padding:3px 10px;border-radius:7px;font-weight:600; }":
        "QLabel { color:#53677C;background:#E7EDF3;padding:3px 10px;border-radius:7px;font-weight:600; }");
    QString health=m_vm->healthDetail();
    if(connected && m_vm->health()==communication::Health::Ready)health=s.simulation?"模拟通道 · 不访问物理总线":"总线活动";
    if(connected && m_vm->health()==communication::Health::Sleeping)health="总线休眠 · 硬件连接正常";
    if(connected && !s.simulation && m_vm->health()==communication::Health::BusWarning)health="总线警告 · "+health;
    m_busHealth->setText(health);m_busHealth->setToolTip(m_vm->healthDetail());
    m_device->setText("—");m_handle->setText("—");
    const auto list=m_vm->hardware();
    for(const auto &h:list)if(h.key==m_software->currentData().toString()){
        m_device->setText(QString("%1 / %2").arg(h.deviceId).arg(h.controller));
        m_handle->setText(QString("0x%1").arg(h.handle,0,16));break;
    }
    m_parameters->setEnabled(!m_vm->hardwareLocked());m_reconnect->setEnabled(!m_vm->hardwareLocked());
    m_protocol->setEnabled(!m_vm->hardwareLocked());
    m_images->setEnabled(!m_vm->busy());m_flashPath->setEnabled(s.flashRequired);m_browseFlash->setEnabled(s.flashRequired);
    m_flashInfo->setEnabled(s.flashRequired);m_refresh->setEnabled(!m_vm->pending());
    m_connect->setText(m_vm->pending()?"处理中…":(connected?"断开":"连接"));
    m_connect->setEnabled(!m_vm->pending() && (connected || m_vm->canConnect()));
    m_start->setText(s.simulation?"开始模拟下载":"开始下载");m_start->setEnabled(m_vm->canStart());
    m_start->setToolTip(m_vm->startHint());m_cancel->setEnabled(m_vm->taskState()==TaskState::Running || m_vm->scanning());
    m_scan->setEnabled(m_vm->canScan());m_scan->setText(m_vm->scanning()?"正在扫描…":"扫描帧头");
    m_scan->setToolTip(m_vm->scanText().isEmpty()?"仅发送 00–3B、3D 帧头，检测从节点响应":m_vm->scanText());
    m_progress->setValue(m_vm->progress());m_task->setText(m_vm->taskText());
    m_flashInfo->setText(imageDescription(s.flashPath));m_appInfo->setText(imageDescription(s.applicationPath));
    m_flashInfo->setToolTip(m_flashInfo->text());m_appInfo->setToolTip(m_appInfo->text());
    m_hint->clear();m_hint->hide();
    m_error->setText(m_vm->error());m_error->setVisible(!m_vm->error().isEmpty());
    if(m_vm->taskState()==TaskState::Running && m_lastTask!=TaskState::Running){
        m_elapsed.restart();m_elapsedText->setText("0.0 s");
    }else if(m_lastTask==TaskState::Running&&m_vm->taskState()!=TaskState::Running&&m_elapsed.isValid()){
        m_elapsedText->setText(QString("%1 s").arg(m_elapsed.elapsed()/1000.0,0,'f',1));
    }
    m_lastTask=m_vm->taskState();
    if(!m_loading && !m_applying && !m_vm->hardwareLocked() && m_vm->communicationIndicator()!=2 &&
       m_mode->currentIndex()==int(s.simulation) && s.hardwareKey!=m_software->currentData().toString())applyForm();
}
void ChannelPage::browseImage(bool flash) {
    const QString current=flash?m_vm->settings().flashPath:m_vm->settings().applicationPath;
    const QString start=current.isEmpty()?QDir::homePath():QFileInfo(current).absolutePath();
    const QString path=QFileDialog::getOpenFileName(this,"选择镜像",start,"Firmware (*.bin *.hex *.BIN *.HEX)");
    if(!path.isEmpty())m_vm->chooseImage(flash,path);
}
void ChannelPage::exportFrames(){
    const auto file=QFileDialog::getSaveFileName(this,"导出当前缓存报文",m_vm->settings().softwareId+"-frames.csv","CSV (*.csv)");
    if(file.isEmpty())return;QString error;
    m_vm->log(m_vm->frames()->exportCsv(file,error)?"已导出报文："+file:error);
}
void ChannelPage::exportLogs(){
    const auto file=QFileDialog::getSaveFileName(this,"导出运行日志",m_vm->settings().softwareId+"-log.txt","Text (*.txt)");
    if(file.isEmpty())return;QString error;m_vm->log(m_vm->exportLogs(file,error)?"已导出日志："+file:error);
}
}
