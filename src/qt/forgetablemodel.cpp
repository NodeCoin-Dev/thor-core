// Copyright (c) 2011-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/forgetablemodel.h>

#include <qt/bitcoinunits.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/forgedialog.h>  // For formatLargeNoLocale()

#include <clientversion.h>
#include <streams.h>

#include <util.h>

ForgeTableModel::ForgeTableModel(const PlatformStyle *_platformStyle, CWallet *wallet, WalletModel *parent) : platformStyle(_platformStyle), QAbstractTableModel(parent), walletModel(parent)
{
    Q_UNUSED(wallet);

    // Set column headings
    columns << tr("Created") << tr("Hammer count") << tr("Hammer status") << tr("Estimated time until status change") << tr("Hammer cost") << tr("Rewards earned");

    sortOrder = Qt::DescendingOrder;
    sortColumn = 0;

    rewardsPaid = cost = profit = 0;
    created = ready = dead = blocksFound = 0; 
}

ForgeTableModel::~ForgeTableModel() {
    // Empty destructor
}

void ForgeTableModel::updateBCTs(bool includeDeadHammers) {
    if (walletModel) {
        // Clear existing
        beginResetModel();
        list.clear();
        endResetModel();

        // Load entries from wallet
        std::vector<CHammerCreationTransactionInfo> vHammerCreationTransactions;
        walletModel->getBCTs(vHammerCreationTransactions, includeDeadHammers);
        beginInsertRows(QModelIndex(), 0, 0);
        created = 0, ready = 0, dead = 0, blocksFound = 0;
        cost = rewardsPaid = profit = 0;
        for (const CHammerCreationTransactionInfo& bct : vHammerCreationTransactions) {
            if (bct.hammerStatus == "ready")
                ready += bct.hammerCount;
            else if (bct.hammerStatus == "created")
                created += bct.hammerCount;
            else if (bct.hammerStatus == "destroyed")
                dead += bct.hammerCount;

            blocksFound += bct.blocksFound;
            cost += bct.hammerFeePaid;
            rewardsPaid += bct.rewardsPaid;
            profit += bct.profit;

            list.prepend(bct);
        }
        endInsertRows();

        // Maintain correct sorting
        sort(sortColumn, sortOrder);

        // Fire signal
        QMetaObject::invokeMethod(walletModel, "newForgeSummaryAvailable", Qt::QueuedConnection);
    }
}

void ForgeTableModel::getSummaryValues(int &_created, int &_ready, int &_dead, int &_blocksFound, CAmount &_cost, CAmount &_rewardsPaid, CAmount &_profit) {
    _created = created;
    _ready = ready;
    _blocksFound = blocksFound;
    _cost = cost;
    _rewardsPaid = rewardsPaid;
    _dead = dead;
    _profit = profit;
}

int ForgeTableModel::rowCount(const QModelIndex &parent) const {
    Q_UNUSED(parent);

    return list.length();
}

int ForgeTableModel::columnCount(const QModelIndex &parent) const {
    Q_UNUSED(parent);

    return columns.length();
}

QVariant ForgeTableModel::data(const QModelIndex &index, int role) const {
    if(!index.isValid() || index.row() >= list.length())
        return QVariant();

    const CHammerCreationTransactionInfo *rec = &list[index.row()];
    if(role == Qt::DisplayRole || role == Qt::EditRole) {        
        switch(index.column()) {
            case Created:
                return (rec->time == 0) ? "Not in chain yet" : GUIUtil::dateTimeStr(rec->time);
            case Count:
                return ForgeDialog::formatLargeNoLocale(rec->hammerCount);
            case Status:
                {
                    QString status = QString::fromStdString(rec->hammerStatus);
                    status[0] = status[0].toUpper();
                    return status;
                }
            case EstimatedTime:
                {
                    QString status = "";
                    if (rec->hammerStatus == "created") {
                        int blocksTillReady = rec->blocksLeft - Params().GetConsensus().hammerLifespanBlocks;
                        status = "Readys in " + QString::number(blocksTillReady) + " blocks (" + secondsToString(blocksTillReady * Params().GetConsensus().nPowTargetSpacing / 2) + ")";
                    } else if (rec->hammerStatus == "ready")
                        status = "Expires in " + QString::number(rec->blocksLeft) + " blocks (" + secondsToString(rec->blocksLeft * Params().GetConsensus().nPowTargetSpacing / 2) + ")";
                    return status;
                }
            case Cost:
                return BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), rec->hammerFeePaid) + " " + BitcoinUnits::shortName(this->walletModel->getOptionsModel()->getDisplayUnit());
            case Rewards:
                if (rec->blocksFound == 0)
                    return "No blocks mined";
                return BitcoinUnits::format(walletModel->getOptionsModel()->getDisplayUnit(), rec->rewardsPaid)
                    + " " + BitcoinUnits::shortName(this->walletModel->getOptionsModel()->getDisplayUnit()) 
                    + " (" + QString::number(rec->blocksFound) + " blocks mined)";
        }
    } else if (role == Qt::TextAlignmentRole) {
        /*if (index.column() == Rewards && rec->blocksFound == 0)
            return (int)(Qt::AlignCenter|Qt::AlignVCenter);
        else*/ if (index.column() == Cost || index.column() == Rewards || index.column() == Count)
            return (int)(Qt::AlignRight|Qt::AlignVCenter);
        else
            return (int)(Qt::AlignCenter|Qt::AlignVCenter);
    } else if (role == Qt::ForegroundRole) {
        const CHammerCreationTransactionInfo *rec = &list[index.row()];

        if (index.column() == Rewards) {
            if (rec->blocksFound == 0)
                return QColor(200, 0, 0);
            if (rec->profit < 0)
                return QColor(170, 70, 0);
            return QColor(27, 170, 45);
        }
        
        if (index.column() == Status) {
            if (rec->hammerStatus == "destroyed")
                return QColor(200, 0, 0);
            if (rec->hammerStatus == "created")
                return QColor(170, 70, 0);
            return QColor(27, 170, 45);
        }

        return QColor(0, 0, 0);
    } else if (role == Qt::DecorationRole) {
        const CHammerCreationTransactionInfo *rec = &list[index.row()];
        if (index.column() == Status) {
            QString iconStr = ":/icons/hammerstatus_dead";    // Dead
            if (rec->hammerStatus == "ready")
                iconStr = ":/icons/hammerstatus_ready";
            else if (rec->hammerStatus == "created")
                iconStr = ":/icons/hammerstatus_created";                
            return platformStyle->SingleColorIcon(iconStr);
        }
    }
    return QVariant();
}

bool ForgeTableModel::setData(const QModelIndex &index, const QVariant &value, int role) {
    return true;
}

QVariant ForgeTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if(orientation == Qt::Horizontal)
        if(role == Qt::DisplayRole && section < columns.size())
            return columns[section];

    return QVariant();
}

void ForgeTableModel::sort(int column, Qt::SortOrder order) {
    sortColumn = column;
    sortOrder = order;
    qSort(list.begin(), list.end(), CHammerCreationTransactionInfoLessThan(column, order));
    Q_EMIT dataChanged(index(0, 0, QModelIndex()), index(list.size() - 1, NUMBER_OF_COLUMNS - 1, QModelIndex()));
}

bool CHammerCreationTransactionInfoLessThan::operator()(CHammerCreationTransactionInfo &left, CHammerCreationTransactionInfo &right) const {
    CHammerCreationTransactionInfo *pLeft = &left;
    CHammerCreationTransactionInfo *pRight = &right;
    if (order == Qt::DescendingOrder)
        std::swap(pLeft, pRight);

    switch(column) {
        case ForgeTableModel::Count:
            return pLeft->hammerCount < pRight->hammerCount;
        case ForgeTableModel::Status:
        case ForgeTableModel::EstimatedTime:
            return pLeft->blocksLeft < pRight->blocksLeft;
        case ForgeTableModel::Cost:
            return pLeft->hammerFeePaid < pRight->hammerFeePaid;
        case ForgeTableModel::Rewards:
            return pLeft->rewardsPaid < pRight->rewardsPaid;
        case ForgeTableModel::Created:
        default:
            return pLeft->time < pRight->time;
    }
}

QString ForgeTableModel::secondsToString(qint64 seconds) {
    const qint64 DAY = 86400;
    qint64 days = seconds / DAY;
    QTime t = QTime(0,0).addSecs(seconds % DAY);
    return QString("%1 days %2 hrs %3 mins").arg(days).arg(t.hour()).arg(t.minute());
}
