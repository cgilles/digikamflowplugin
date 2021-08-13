#include "PicFlowView.h"

// 标准库
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <list>
#include <semaphore>

// Qt 库
#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QImage>
#include <QImageReader>
#include <QInputDialog>
#include <QLabel>
#include <QList>
#include <QMenu>
#include <QPointer>
#include <QRunnable>
#include <QScrollArea>
#include <QThread>
#include <QThreadPool>
#include <QTranslator>

// digikam 库
#include <digikam/coredbaccess.h>
#include <digikam/dbengineaction.h>
#include <digikam/dinfointerface.h>
#include <digikam/dmessagebox.h>

// 本地库
#include <flowlayout.h>
#include <qaction.h>
#include <qnamespace.h>
#include <qpixmap.h>

#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#define CHECK_CLOSE_FOR_BREAK(msg) \
    if(stop_) {                    \
        spdlog::debug(msg);        \
        break;                     \
    }
namespace Cathaysia {

PicFlowView::PicFlowView(QObject* const parent) : DPluginGeneric { parent } {
    spdlog::set_pattern("%^%l%$: %v");
#ifdef QT_DEBUG
    spdlog::set_level(spdlog::level::debug);
#endif
}

PicFlowView::~PicFlowView() noexcept { }

QString PicFlowView::name() const {
    return tr("PicFlowPlugin");
}

QString PicFlowView::iid() const {
    return QLatin1String(DPLUGIN_IID);
}

QIcon PicFlowView::icon() const {
    return QIcon::fromTheme(QLatin1String("digikam"));
}

QString PicFlowView::description() const {
    return tr("Show pictures in a mono flow view");
}

QString PicFlowView::details() const {
    return tr("A plugin provide Flow view");
}

// clang-format off
QList<Digikam::DPluginAuthor> PicFlowView::authors() const {
    return QList<DPluginAuthor>()
            << DPluginAuthor(QString::fromUtf8("Cathaysia"),
                             QString::fromUtf8("319513897@outlook.com"),
                             QString::fromUtf8("(c) 2021"))
            ;
}
// clang-format on

// 窗口大小发生变化时自动更新布局
bool PicFlowView::eventFilter(QObject* watched, QEvent* event) {
    auto dialog = qobject_cast<QDialog*>(watched);
    if(!dialog) return false;
    if(event->type() == QEvent::Resize) {
        auto content = dialog->findChild<Z::FlowLayout*>("FlowLayout");
        content->parentWidget()->resize(dialog->width(), content->innerHeight());
        return true;
    } else if(event->type() == QEvent::Close) {
        // @TODO 这个关闭按钮是全局的，因此一个正在窗口关闭应该会导致所有生产线程停止加载数据
        stop_ = true;
        return true;
    } else if(event->type() == QEvent::Show) {
        stop_ = false;
        return true;
    }
    return false;
}
// 插件的基本设置
void PicFlowView::setup(QObject* const parent) {
    DPluginAction* const ac = new DPluginAction(parent);
    ac->setIcon(icon());
    ac->setObjectName(QLatin1String("PiclLowView"));
    ac->setActionCategory(DPluginAction::ActionCategory::GenericView);
    ac->setText("PicFlowView");
    // 添加菜单项
    // @TODO 这里有内存泄漏吗？
    auto setting = new QMenu;
    // 参考宽度设置
    auto widthAction = setting->addAction(tr("设置参考宽度"));
    widthAction->setWhatsThis(tr("设置图片的参考宽度，图片的宽度会在更<b>倾向于</b>选择此宽度"));
    // 添加设置
    connect(widthAction, &QAction::triggered, [this]() {
        bool ok = false;
        auto result = QInputDialog::getDouble(nullptr, tr("输入参考宽度"), tr("参考宽度"), width_, -1, 9999, 1, &ok);
        if(ok) {
            this->width_ = result;
            emit widthChanged(result);
        };
    });
    // 缩放设置
    auto scaledAction = setting->addAction(tr("缩放图片"));
    scaledAction->setCheckable(true);
    scaledAction->setChecked(true);
    connect(scaledAction, &QAction::triggered, [this](bool enabledIt) {
        this->enable_scaled_ = enabledIt;
    });
    // spacing 设置
    auto spac = setting->addAction("设置图片间的间隔");
    connect(spac, &QAction::triggered, [this]() {
        bool ok = false;
        auto result = QInputDialog::getInt(nullptr, tr("输入参考宽度"), tr("参考宽度"), spacing_, 0, 9999, 1, &ok);
        if(ok) {
            this->spacing_ = result;
            emit spacingChanged(result);
        };
    });
    ac->setMenu(setting);
    connect(ac, &DPluginAction::triggered, this, &PicFlowView::flowView);
    addAction(ac);
}

// 构建一个对话框
Cathaysia::PicFlowView::ShareData PicFlowView::getShareData() {
    // 从外到内依次是
    // mainDialog -> dialogLayout -> scrollWidget -> box -> flowLayout
    auto mainDialog   = new QDialog;
    auto dialogLayout = new QHBoxLayout(mainDialog);
    auto scrollWidget = new QScrollArea(mainDialog);
    auto box          = new QWidget(mainDialog);
    auto flowLayout   = new Z::FlowLayout(box);
    // 关闭窗口时释放内存
    mainDialog->setAttribute(Qt::WA_DeleteOnClose, true);
    mainDialog->installEventFilter(this);

    dialogLayout->addWidget(scrollWidget);

    scrollWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollWidget->setWidget(box);

    box->setLayout(flowLayout);

    flowLayout->setObjectName("FlowLayout");
    flowLayout->setSpacing(spacing_);

    //设置图片的参考宽度
    flowLayout->setWidgetWidth(width_);
    //
    connect(this, &PicFlowView::widthChanged, flowLayout, &Z::FlowLayout::setWidgetWidth);
    connect(this, &PicFlowView::spacingChanged, flowLayout, &Z::FlowLayout::setSpacing);
    return ShareData(mainDialog, flowLayout);
}

void PicFlowView::flowView() {
    auto* const iface = infoIface(sender());
    /**
     * @todo 1. 是否让多个窗口共用一个锁（现在不是）
     * 在经过几次测试后，我发现要阻止消费者进入临界区几乎不可避免地会出现死锁
     * 既然如此，就在队列末尾添加一个空对象用作判断，closeEvent 只会影响生产者
     * 而我只需要保证消费者后于生产者退出即可
     */
    auto shareData  = getShareData();
    auto mainDialog = shareData.first;
    auto flowLayout = shareData.second;
    // 先显示
    mainDialog->resize(800, 600);
    mainDialog->show();

    std::list<QPixmap> imgBuf;
    // 防止由于刚开始 imgBuf.length == 0 导致无法进入消费者
    std::atomic_bool over = false;

    std::binary_semaphore semMutex(1);
    std::binary_semaphore empty(10);    // 最多允许多少个生产线程进入临界区
    std::binary_semaphore full(0);
    // 用于执行任务的 Lambda
    auto task = ([&semMutex, &empty, &full, &imgBuf, &iface, &over, this](const QUrl& item) {
        QString imgPath = item.toString().replace("file://", "");
        if(stop_) {
            spdlog::debug("中断生产线程 1");
            over = true;
            return;
        }
        spdlog::debug("producer: 加载 {} ", imgPath.toStdString());
        // QPixmap 存在隐式数据共享，因此无需智能指针
        QPixmap pix(imgPath);
        // 对图片进行缩放以改善内存占用情况
        // pix = pix.scaled(1344,756, Qt::KeepAspectRatio, Qt::FastTransformation).scaled(960,540,
        //         Qt::KeepAspectRatio, Qt::SmoothTransformation);
        // 一个更小的图片缩放到 1920x1080 会导致占用内存变大吗？是的，会变大
        if(enable_scaled_ && (pix.width() * pix.height() > 1928 * 1080))
            pix = pix.scaled(1920, 1080, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if(stop_) {
            spdlog::debug("中断生产线程 2");
            return;
        }
        // 在经过几次测试后，我发现在接受到 stop_ 后，生产线程总是在上面停止，而不会在下面
        if(!pix.isNull()) {
            empty.acquire();
            semMutex.acquire();
            imgBuf.push_back(pix);

            semMutex.release();
            full.release();
        }
    });

    // join 可以防止由于插件关闭导致的主窗口关闭
    std::jthread producer([&]() {
        QThreadPool localPool;
        for(auto const& item: iface->currentAlbumItems()) localPool.start(std::bind(task, item));
        spdlog::debug("等待生产线程结束");
        localPool.waitForDone();
        QPixmap nullpix;
        imgBuf.push_back(nullpix);
        full.release();
        spdlog::debug("生产进程完成");
        over = true;
    });

    // 先检查是否有有效数据
    bool hasVaildImg    = false;
    auto supportFormats = QImageReader::supportedImageFormats();
    for(auto& item: iface->currentAlbumItems()) {
        for(auto& suffix: supportFormats) {
            hasVaildImg = item.toString().endsWith(suffix);
            if(hasVaildImg) break;
        }
    }
    if(!hasVaildImg) {
        spdlog::info("没有有效数据，退出");
        this->stop_ = true;
        return;
    }
    // 在主线程中将 QImage 添加到 GUI 中
    size_t counter = 0;
    while(!over || imgBuf.size()) {
        // 防止程序被中断后依然尝试获取资源
        // if(over) return;
        spdlog::debug("第 {} 次消费", counter);
        QLabel* img = new QLabel();
        // 进入临界区
        full.acquire();
        semMutex.acquire();
        if(imgBuf.front().isNull()) {
            spdlog::debug("检测到空对象退出");
            break;
        }
        img->setPixmap(imgBuf.front());
        imgBuf.pop_front();

        semMutex.release();
        empty.release();
        // 离开临界区
        img->setScaledContents(true);
        flowLayout->addWidget(img);
        // 防止界面卡顿
        qApp->processEvents();
        spdlog::debug("第 {} 次消费完成", counter);
        ++counter;
    }
    spdlog::debug("图片加载完成");
}
}    // namespace Cathaysia
