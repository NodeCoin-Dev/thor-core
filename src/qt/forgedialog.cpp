// Copyright (c) 2011-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallet.h>
#include <wallet/fees.h>

#include <qt/forgedialog.h>
#include <qt/forms/ui_forgedialog.h>
#include <qt/clientmodel.h>
#include <qt/sendcoinsdialog.h>

#include <qt/addressbookpage.h>
#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/receiverequestdialog.h>
#include <qt/forgetablemodel.h>
#include <qt/walletmodel.h>
#include <qt/tinypie.h>
#include <qt/qcustomplot.h>

#include <qt/optionsdialog.h>

#include <QAction>
#include <QCursor>
#include <QMessageBox>
#include <QScrollBar>
#include <QTextDocument>

#include <util.h>
#include <validation.h>

ForgeDialog::ForgeDialog(const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ForgeDialog),
    columnResizingFixer(0),
    model(0),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    if (!_platformStyle->getImagesOnButtons())
        ui->createHammersButton->setIcon(QIcon());
    else
        ui->createHammersButton->setIcon(_platformStyle->SingleColorIcon(":/icons/hammer"));

    hammerCost = totalCost = rewardsPaid = cost = profit = 0;
    created = ready = dead = blocksFound = 0;
    lastGlobalCheckHeight = 0;
    potentialRewards = 0;
    currentBalance = 0;
    hammerPopIndex = 0;

    ui->globalForgeSummaryError->hide();

    ui->hammerPopIndexPie->foregroundCol = Qt::red;

    // Swap cols for forge weight pie
    QColor temp = ui->forgeWeightPie->foregroundCol;
    ui->forgeWeightPie->foregroundCol = ui->forgeWeightPie->backgroundCol;
    ui->forgeWeightPie->backgroundCol = temp;
    ui->forgeWeightPie->borderCol = palette().color(backgroundRole());

    initGraph();
    ui->hammerPopGraph->hide();
}

void ForgeDialog::setClientModel(ClientModel *_clientModel) {
    this->clientModel = _clientModel;

    if (_clientModel) {
        connect(_clientModel, SIGNAL(numBlocksChanged(int,QDateTime,double,bool)), this, SLOT(updateData()));
        connect(_clientModel, SIGNAL(numConnectionsChanged(int)), this, SLOT(updateData()));    // TODO: This may be too expensive to call here, and the only point is to update the forge status icon.
    }
}

void ForgeDialog::setModel(WalletModel *_model) {
    this->model = _model;

    if(_model && _model->getOptionsModel())
    {
        _model->getForgeTableModel()->sort(ForgeTableModel::Created, Qt::DescendingOrder);
        connect(_model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));
        updateDisplayUnit();

        setBalance(_model->getBalance(), _model->getUnconfirmedBalance(), _model->getCreatedBalance(), _model->getWatchBalance(), _model->getWatchUnconfirmedBalance(), _model->getWatchCreatedBalance());
        connect(_model, SIGNAL(balanceChanged(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)), this, SLOT(setBalance(CAmount,CAmount,CAmount,CAmount,CAmount,CAmount)));
        
        if (_model->getEncryptionStatus() != WalletModel::Locked)
            ui->releaseSwarmButton->hide();
        connect(_model, SIGNAL(encryptionStatusChanged(int)), this, SLOT(setEncryptionStatus(int)));

        QTableView* tableView = ui->currentForgeView;

        tableView->verticalHeader()->hide();
        tableView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        tableView->setModel(_model->getForgeTableModel());
        tableView->setAlternatingRowColors(true);
        tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
        tableView->setSelectionMode(QAbstractItemView::ContiguousSelection);
        tableView->setColumnWidth(ForgeTableModel::Created, CREATED_COLUMN_WIDTH);
        tableView->setColumnWidth(ForgeTableModel::Count, COUNT_COLUMN_WIDTH);
        tableView->setColumnWidth(ForgeTableModel::Status, STATUS_COLUMN_WIDTH);
        tableView->setColumnWidth(ForgeTableModel::EstimatedTime, TIME_COLUMN_WIDTH);
        tableView->setColumnWidth(ForgeTableModel::Cost, COST_COLUMN_WIDTH);
        tableView->setColumnWidth(ForgeTableModel::Rewards, REWARDS_COLUMN_WIDTH);
        //tableView->setColumnWidth(ForgeTableModel::Profit, PROFIT_COLUMN_WIDTH);

        // Last 2 columns are set by the columnResizingFixer, when the table geometry is ready.
        //columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(tableView, PROFIT_COLUMN_WIDTH, FORGE_COL_MIN_WIDTH, this);
        columnResizingFixer = new GUIUtil::TableViewLastColumnResizingFixer(tableView, REWARDS_COLUMN_WIDTH, FORGE_COL_MIN_WIDTH, this);

        // Populate initial data
        updateData(true);
    }
}

ForgeDialog::~ForgeDialog() {
    delete ui;
}

void ForgeDialog::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& createdBalance, const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchCreatedBalance) {
    currentBalance = balance;
    setAmountField(ui->currentBalance, currentBalance);
}

void ForgeDialog::setEncryptionStatus(int status) {
    switch(status) {
        case WalletModel::Unencrypted:
        case WalletModel::Unlocked:
            ui->releaseSwarmButton->hide();
            break;
        case WalletModel::Locked:
            ui->releaseSwarmButton->show();
            break;
    }
    updateData();
}

void ForgeDialog::setAmountField(QLabel *field, CAmount value) {
    field->setText(
        BitcoinUnits::format(model->getOptionsModel()->getDisplayUnit(), value)
        + " "
        + BitcoinUnits::shortName(model->getOptionsModel()->getDisplayUnit())
    );
}

QString ForgeDialog::formatLargeNoLocale(int i) {
    QString i_str = QString::number(i);

    // Use SI-style thin space separators as these are locale independent and can't be confused with the decimal marker.
    QChar thin_sp(THIN_SP_CP);
    int i_size = i_str.size();
    for (int i = 3; i < i_size; i += 3)
        i_str.insert(i_size - i, thin_sp);

    return i_str;
}

void ForgeDialog::updateData(bool forceGlobalSummaryUpdate) {
    if(IsInitialBlockDownload() || chainActive.Height() == 0) {
        ui->globalForgeSummary->hide();
        ui->globalForgeSummaryError->show();
        return;
    }
    
    const Consensus::Params& consensusParams = Params().GetConsensus();

    if(model && model->getForgeTableModel()) {
        model->getForgeTableModel()->updateBCTs(ui->includeDeadHammersCheckbox->isChecked());
        model->getForgeTableModel()->getSummaryValues(created, ready, dead, blocksFound, cost, rewardsPaid, profit);
        
        // Update labels
        setAmountField(ui->rewardsPaidLabel, rewardsPaid);
        setAmountField(ui->costLabel, cost);
        setAmountField(ui->profitLabel, profit);
        ui->readyLabel->setText(formatLargeNoLocale(ready));
        ui->createdLabel->setText(formatLargeNoLocale(created));
        ui->blocksFoundLabel->setText(QString::number(blocksFound));

        if(dead == 0) {
            ui->deadLabel->hide();
            ui->deadTitleLabel->hide();
            ui->deadLabelSpacer->changeSize(0, 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
        } else {
            ui->deadLabel->setText(formatLargeNoLocale(dead));
            ui->deadLabel->show();
            ui->deadTitleLabel->show();
            ui->deadLabelSpacer->changeSize(ui->createdLabelSpacer->geometry().width(), 0, QSizePolicy::Fixed, QSizePolicy::Fixed);
        }

        // Set icon and tooltip for tray icon
        QString tooltip, icon;
        if (clientModel && clientModel->getNumConnections() == 0) {
            tooltip = "Thor is not connected";
            icon = ":/icons/forgestatus_disabled";
        } else if (!model->isForgeEnabled()) {
            tooltip = "The Forge is not enabled on the network";
            icon = ":/icons/forgestatus_disabled";
        } else {
            if (ready + created == 0) {
                tooltip = "No live hammers currently in wallet";
                icon = ":/icons/forgestatus_clear";
            } else if (ready == 0) {
                tooltip = "Only created hammers currently in wallet";
                icon = ":/icons/forgestatus_orange";
            } else {
                if (model->getEncryptionStatus() == WalletModel::Locked) {
                    tooltip = "WARNING: Hammers ready but not mining because wallet is locked";
                    icon = ":/icons/forgestatus_red";
                } else {
                    tooltip = "Hammers ready and mining";
                    icon = ":/icons/forgestatus_green";
                }
            }
        }
        // Now update bitcoingui
        Q_EMIT forgeStatusIconChanged(icon, tooltip);
    }

    hammerCost = GetHammerCost(chainActive.Tip()->nHeight, consensusParams);
    setAmountField(ui->hammerCostLabel, hammerCost);
    updateTotalCostDisplay();

    if (forceGlobalSummaryUpdate || chainActive.Tip()->nHeight >= lastGlobalCheckHeight + 10) { // Don't update global summary every block
        int globalCreatedHammers, globalCreatedBCTs, globalReadyHammers, globalReadyBCTs;
        if (!GetNetworkForgeInfo(globalCreatedHammers, globalCreatedBCTs, globalReadyHammers, globalReadyBCTs, potentialRewards, consensusParams, true)) {
            ui->globalForgeSummary->hide();
            ui->globalForgeSummaryError->show();
        } else {
            ui->globalForgeSummaryError->hide();
            ui->globalForgeSummary->show();
            if (globalCreatedHammers == 0)
                ui->globalCreatedLabel->setText("0");
            else
                ui->globalCreatedLabel->setText(formatLargeNoLocale(globalCreatedHammers) + " (" + QString::number(globalCreatedBCTs) + " transactions)");

            if (globalReadyHammers == 0)
                ui->globalReadyLabel->setText("0");
            else
                ui->globalReadyLabel->setText(formatLargeNoLocale(globalReadyHammers) + " (" + QString::number(globalReadyBCTs) + " transactions)");

            updateGraph();
        }

        setAmountField(ui->potentialRewardsLabel, potentialRewards);

        double forgeWeight = ready / (double)globalReadyHammers;
        ui->localForgeWeightLabel->setText((ready == 0 || globalReadyHammers == 0) ? "0" : QString::number(forgeWeight, 'f', 3));
        ui->forgeWeightPie->setValue(forgeWeight);

        hammerPopIndex = ((hammerCost * globalReadyHammers) / (double)potentialRewards) * 100.0;
        if (hammerPopIndex > 200) hammerPopIndex = 200;
        ui->hammerPopIndexLabel->setText(QString::number(floor(hammerPopIndex)));
        ui->hammerPopIndexPie->setValue(hammerPopIndex / 100);
        
        lastGlobalCheckHeight = chainActive.Tip()->nHeight;
    }

    ui->blocksTillGlobalRefresh->setText(QString::number(10 - (chainActive.Tip()->nHeight - lastGlobalCheckHeight)));
}

void ForgeDialog::updateDisplayUnit() {
    if(model && model->getOptionsModel()) {
        setAmountField(ui->hammerCostLabel, hammerCost);
        setAmountField(ui->rewardsPaidLabel, rewardsPaid);
        setAmountField(ui->costLabel, cost);
        setAmountField(ui->profitLabel, profit);
        setAmountField(ui->potentialRewardsLabel, potentialRewards);
        setAmountField(ui->currentBalance, currentBalance);
        setAmountField(ui->totalCostLabel, totalCost);
    }

    updateTotalCostDisplay();
}

void ForgeDialog::updateTotalCostDisplay() {    
    totalCost = hammerCost * ui->hammerCountSpinner->value();

    if(model && model->getOptionsModel()) {
        setAmountField(ui->totalCostLabel, totalCost);
        
        if (totalCost > model->getBalance())
            ui->hammerCountSpinner->setStyleSheet("QSpinBox{background:#FF8080;}");
        else
            ui->hammerCountSpinner->setStyleSheet("QSpinBox{background:white;}");
    }
}

void ForgeDialog::on_hammerCountSpinner_valueChanged(int i) {
    updateTotalCostDisplay();
}

void ForgeDialog::on_includeDeadHammersCheckbox_stateChanged() {
    updateData();
}

void ForgeDialog::on_showAdvancedStatsCheckbox_stateChanged() {
    if(ui->showAdvancedStatsCheckbox->isChecked())
        ui->hammerPopGraph->show();
    else
        ui->hammerPopGraph->hide();
}

void ForgeDialog::on_retryGlobalSummaryButton_clicked() {
    updateData(true);
}

void ForgeDialog::on_refreshGlobalSummaryButton_clicked() {
    updateData(true);
}

void ForgeDialog::on_releaseSwarmButton_clicked() {
    if(model)
        model->requestUnlock(true);
}

void ForgeDialog::on_createHammersButton_clicked() {
    if (model) {
        if (totalCost > model->getBalance()) {
            QMessageBox::critical(this, tr("Error"), tr("Insufficient balance to create hammers."));
            return;
        }
		WalletModel::UnlockContext ctx(model->requestUnlock());
		if(!ctx.isValid())
			return;     // Unlock wallet was cancelled
        model->createHammers(ui->hammerCountSpinner->value(), ui->donateCommunityFundCheckbox->isChecked(), this, hammerPopIndex);
    }
}

// LitecoinCash: Hive: Mining optimisations: Shortcut to Hive mining options
void ForgeDialog::on_showForgeOptionsButton_clicked() {
    if(!clientModel || !clientModel->getOptionsModel())
        return;

    OptionsDialog dlg(this, model->isWalletEnabled());
    dlg.setModel(clientModel->getOptionsModel());
    dlg.exec();
}

void ForgeDialog::initGraph() {
    ui->hammerPopGraph->addGraph();
    ui->hammerPopGraph->graph(0)->setLineStyle(QCPGraph::lsLine);
    ui->hammerPopGraph->graph(0)->setPen(QPen(Qt::blue));
    QColor color(42, 67, 182);
    color.setAlphaF(0.35);
    ui->hammerPopGraph->graph(0)->setBrush(QBrush(color));

    ui->hammerPopGraph->addGraph();
    ui->hammerPopGraph->graph(1)->setLineStyle(QCPGraph::lsLine);
    ui->hammerPopGraph->graph(1)->setPen(QPen(Qt::black));
    QColor color1(42, 182, 67);
    color1.setAlphaF(0.35);
    ui->hammerPopGraph->graph(1)->setBrush(QBrush(color1));

    QSharedPointer<QCPAxisTickerDateTime> dateTicker(new QCPAxisTickerDateTime);
    dateTicker->setTickStepStrategy(QCPAxisTicker::TickStepStrategy::tssMeetTickCount);
    dateTicker->setTickCount(8);
    dateTicker->setDateTimeFormat("ddd d MMM");
    ui->hammerPopGraph->xAxis->setTicker(dateTicker);

    ui->hammerPopGraph->yAxis->setLabel("Hammers");

    giTicker = QSharedPointer<QCPAxisTickerGI>(new QCPAxisTickerGI);
    ui->hammerPopGraph->yAxis2->setTicker(giTicker);
    ui->hammerPopGraph->yAxis2->setLabel("Global index");
    ui->hammerPopGraph->yAxis2->setVisible(true);

    ui->hammerPopGraph->xAxis->setTickLabelFont(QFont(QFont().family(), 8));
    ui->hammerPopGraph->xAxis2->setTickLabelFont(QFont(QFont().family(), 8));
    ui->hammerPopGraph->yAxis->setTickLabelFont(QFont(QFont().family(), 8));
    ui->hammerPopGraph->yAxis2->setTickLabelFont(QFont(QFont().family(), 8));

    connect(ui->hammerPopGraph->xAxis, SIGNAL(rangeChanged(QCPRange)), ui->hammerPopGraph->xAxis2, SLOT(setRange(QCPRange)));
    connect(ui->hammerPopGraph->yAxis, SIGNAL(rangeChanged(QCPRange)), ui->hammerPopGraph->yAxis2, SLOT(setRange(QCPRange)));
    connect(ui->hammerPopGraph, SIGNAL(mouseMove(QMouseEvent*)), this, SLOT(onMouseMove(QMouseEvent*)));

    globalMarkerLine = new QCPItemLine(ui->hammerPopGraph);
    globalMarkerLine->setPen(QPen(Qt::blue, 1, Qt::DashLine));
    
    graphTracerCreated = new QCPItemTracer(ui->hammerPopGraph);
    graphTracerCreated->setGraph(ui->hammerPopGraph->graph(0));
    graphTracerReady = new QCPItemTracer(ui->hammerPopGraph);
    graphTracerReady->setGraph(ui->hammerPopGraph->graph(1));

    graphMouseoverText = new QCPItemText(ui->hammerPopGraph);
}

void ForgeDialog::updateGraph() {
    const Consensus::Params& consensusParams = Params().GetConsensus();

    ui->hammerPopGraph->graph()->data()->clear();
    double now = QDateTime::currentDateTime().toTime_t();
    int totalLifespan = consensusParams.hammerGestationBlocks + consensusParams.hammerLifespanBlocks;
    QVector<QCPGraphData> dataReady(totalLifespan);
    QVector<QCPGraphData> dataCreated(totalLifespan);
    for (int i = 0; i < totalLifespan; i++) {
        dataCreated[i].key = now + consensusParams.nPowTargetSpacing / 2 * i;
        dataCreated[i].value = (double)hammerPopGraph[i].createdPop;

        dataReady[i].key = dataCreated[i].key;
        dataReady[i].value = (double)hammerPopGraph[i].readyPop;
    }
    ui->hammerPopGraph->graph(0)->data()->set(dataCreated);
    ui->hammerPopGraph->graph(1)->data()->set(dataReady);

    double global100 = (double)potentialRewards / hammerCost;
    globalMarkerLine->start->setCoords(now, global100);
    globalMarkerLine->end->setCoords(now + consensusParams.nPowTargetSpacing / 2 * totalLifespan, global100);
    giTicker->global100 = global100;
    ui->hammerPopGraph->rescaleAxes();
    ui->hammerPopGraph->replot();
}

void ForgeDialog::onMouseMove(QMouseEvent *event) {
    QCustomPlot* customPlot = qobject_cast<QCustomPlot*>(sender());

    int x = (int)customPlot->xAxis->pixelToCoord(event->pos().x());
    int y = (int)customPlot->yAxis->pixelToCoord(event->pos().y());

    graphTracerCreated->setGraphKey(x);
    graphTracerReady->setGraphKey(x);
    int hammerCountCreated = (int)graphTracerCreated->position->value();
    int hammerCountReady = (int)graphTracerReady->position->value();      

    QDateTime xDateTime = QDateTime::fromTime_t(x);
    int global100 = (int)((double)potentialRewards / hammerCost);
    QColor traceColReady = hammerCountReady >= global100 ? Qt::red : Qt::black;
    QColor traceColCreated = hammerCountCreated >= global100 ? Qt::red : Qt::black;

    graphTracerCreated->setPen(QPen(traceColCreated, 1, Qt::DashLine));    
    graphTracerReady->setPen(QPen(traceColReady, 1, Qt::DashLine));

    graphMouseoverText->setText(xDateTime.toString("ddd d MMM") + " " + xDateTime.time().toString() + ":\n" + formatLargeNoLocale(hammerCountReady) + " ready hammers\n" + formatLargeNoLocale(hammerCountCreated) + " created hammers");
    graphMouseoverText->setColor(traceColReady);
    graphMouseoverText->position->setCoords(QPointF(x, y));
    QPointF pixelPos = graphMouseoverText->position->pixelPosition();

    int xoffs, yoffs;
    if (ui->hammerPopGraph->height() > 150) {
        graphMouseoverText->setFont(QFont(font().family(), 10));
        xoffs = 80;
        yoffs = 30;
    } else {
        graphMouseoverText->setFont(QFont(font().family(), 8));
        xoffs = 70;
        yoffs = 20;
    }

    if (pixelPos.y() > ui->hammerPopGraph->height() / 2)
        pixelPos.setY(pixelPos.y() - yoffs);
    else
        pixelPos.setY(pixelPos.y() + yoffs);

    if (pixelPos.x() > ui->hammerPopGraph->width() / 2)
        pixelPos.setX(pixelPos.x() - xoffs);
    else
        pixelPos.setX(pixelPos.x() + xoffs);

    
    graphMouseoverText->position->setPixelPosition(pixelPos);

    customPlot->replot();
}

void ForgeDialog::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    columnResizingFixer->stretchColumnWidth(ForgeTableModel::Rewards);
}
