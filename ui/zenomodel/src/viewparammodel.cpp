#include "viewparammodel.h"
#include "parammodel.h"
#include "zassert.h"


VParamItem::VParamItem(VPARAM_TYPE type, const QString& text)
    : QStandardItem(text)
    , vType(type)
{
    m_info.control = CONTROL_NONE;
    m_info.name = text;
}

VParamItem::VParamItem(VPARAM_TYPE type, const QIcon& icon, const QString& text)
    : QStandardItem(icon, text)
    , vType(type)
{
    m_info.control = CONTROL_NONE;
    m_info.name = text;
}

VParamItem::VParamItem(VPARAM_TYPE type)
    : vType(type)
{
    m_info.control = CONTROL_NONE;
}

QVariant VParamItem::data(int role) const
{
    switch (role)
    {
    case Qt::DisplayRole:
    case ROLE_VPARAM_NAME:  return m_info.name;
    case ROLE_VPARAM_TYPE:  return vType;
    case ROLE_PARAM_CTRL:   return m_info.control;
    case ROLE_PARAM_VALUE:
    {
        if (!m_index.isValid())
            return m_info.value;
        return m_index.data(ROLE_PARAM_VALUE);
    }
    case ROLE_PARAM_TYPE:
    {
        if (!m_index.isValid())
            return m_info.typeDesc;
        return m_index.data(ROLE_PARAM_TYPE);
    }
    default:
        return QVariant();
    }
}

void VParamItem::setData(const QVariant& value, int role)
{
    switch (role)
    {
        case ROLE_PARAM_VALUE:
        {
            if (m_index.isValid())
            {
                QAbstractItemModel* pModel = const_cast<QAbstractItemModel*>(m_index.model());
                pModel->setData(m_index, value, role);
            }
            else
            {
                m_info.value = value;
            }
            break;
        }
    }
}

VParamItem* VParamItem::getItem(const QString& uniqueName) const
{
    for (int r = 0; r < rowCount(); r++)
    {
        VParamItem* pChild = static_cast<VParamItem*>(child(r));
        if (pChild->m_info.name == uniqueName)
            return pChild;
    }
    return nullptr;
}

QStandardItem* VParamItem::clone() const
{
    VParamItem* pItem = new VParamItem(vType);
    pItem->m_info = this->m_info;
    pItem->m_index = m_index;
    return pItem;
}

void VParamItem::cloneFrom(VParamItem* rItem)
{
    if (!rItem) return;

    if (rItem->vType == VPARAM_PARAM)
    {
        if (m_index != rItem->m_index)
        {
            m_index = rItem->m_index;
        }
        if (m_info.control != rItem->m_info.control)
        {
            setData(rItem->m_info.control, ROLE_PARAM_CTRL);
        }
        if (m_info.value != rItem->m_info.value)
        {
            setData(rItem->m_info.value, ROLE_PARAM_VALUE);
        }
        return;
    }

    //remove the old items first.
    QVector<int> deleteRows;
    for (int r = 0; r < rowCount(); r++)
    {
        VParamItem* lChild = static_cast<VParamItem*>(child(r));
        VParamItem* rChild = rItem->getItem(lChild->m_info.name);
        if (!rChild)
        {
            deleteRows.append(r);
        }
    }
    for (int r : deleteRows)
    {
        removeRow(r);
    }

    for (int r = 0; r < rItem->rowCount(); r++)
    {
        VParamItem* rChild = static_cast<VParamItem*>(rItem->child(r));
        VParamItem* lChild = getItem(rChild->m_info.name);
        if (!lChild)
        {
            //insert new child.
            VParamItem* newItem = static_cast<VParamItem*>(rChild->clone());
            newItem->cloneFrom(rChild);
            insertRow(r, newItem);
            continue;
        }
        lChild->cloneFrom(rChild);
    }
}

bool VParamItem::operator==(VParamItem* rItem) const
{
    //only itself.
    if (!rItem) return false;
    return (rItem->m_info.name == m_info.name &&
            rItem->m_info.control == m_info.control &&
            rItem->vType == vType &&
            rItem->m_info.typeDesc == m_info.typeDesc &&
            rItem->m_index == m_index);
}



ViewParamModel::ViewParamModel(QObject* parent)
    : QStandardItemModel(parent)
{
    setup("");
}

ViewParamModel::ViewParamModel(const QString& customXml, QObject* parent)
    : QStandardItemModel(parent)
{
    setup(customXml);
}

void ViewParamModel::setup(const QString& customUI)
{
    if (customUI.isEmpty())
    {
        /*default structure:
            root
                |-- Tab (Default)
                    |-- Inputs (Group)
                        -- input param1 (Item)
                        -- input param2
                        ...

                    |-- Params (Group)
                        -- param1 (Item)
                        -- param2 (Item)
                        ...

                    |- Outputs (Group)
                        - output param1 (Item)
                        - output param2 (Item)
            ...
        */
        VParamItem* pRoot = new VParamItem(VPARAM_ROOT, "root");

        VParamItem* pTab = new VParamItem(VPARAM_TAB, "Default");
        {
            VParamItem* pInputsGroup = new VParamItem(VPARAM_GROUP, "In Sockets");
            VParamItem* paramsGroup = new VParamItem(VPARAM_GROUP, "Parameters");
            VParamItem* pOutputsGroup = new VParamItem(VPARAM_GROUP, "Out Sockets");

            pTab->appendRow(pInputsGroup);
            pTab->appendRow(paramsGroup);
            pTab->appendRow(pOutputsGroup);
        }
        pRoot->appendRow(pTab);

        appendRow(pRoot);
    }
}

QString ViewParamModel::exportUI() const
{
    /*
     xml example:

     <customui>
        <node name = "VDBWrangle">
            <tab name = "Default" type="default" hint="default msg for node">
                <group name = "inputs">
                    <param name = "..." control = "..."/>
                    ...
                </group>
                <group name = "params">
                    <param name = "..." control = "..."/>
                </group>
                <group name = "outputs">
                    
                </group>
            </tab>
        </node>
        <node name = ...>
        </node>
     </customui>
    */
    return QString();
}

void ViewParamModel::onParamsInserted(const QModelIndex& parent, int first, int last)
{
    IParamModel* pModel = qobject_cast<IParamModel*>(sender());
    ZASSERT_EXIT(pModel);
    const QModelIndex& idx = pModel->index(first, 0, parent);
    if (!idx.isValid()) return;

    QStandardItem* pRoot = invisibleRootItem();
    PARAM_CLASS cls = pModel->paramClass();
    if (cls == PARAM_INPUT)
    {
        QList<QStandardItem*> lst = findItems("In Sockets", Qt::MatchRecursive | Qt::MatchExactly);
        for (QStandardItem* pItem : lst)
        {
            if (pItem->data(ROLE_VPARAM_TYPE) == VPARAM_GROUP)
            {
                const QString& realName = idx.data(ROLE_PARAM_NAME).toString();
                const QString& displayName = realName;  //todo: mapping name.
                PARAM_CONTROL ctrl = (PARAM_CONTROL)idx.data(ROLE_PARAM_CTRL).toInt();
                VParamItem* paramItem = new VParamItem(VPARAM_PARAM, displayName);
                paramItem->m_info.control = ctrl;
                paramItem->m_index = idx;
                pItem->appendRow(paramItem);
                break;
            }
        }
    }
    else if (cls == PARAM_PARAM)
    {
        QList<QStandardItem*> lst = findItems("Parameters", Qt::MatchRecursive | Qt::MatchExactly);
        for (QStandardItem* pItem : lst)
        {
            if (pItem->data(ROLE_VPARAM_TYPE) == VPARAM_GROUP)
            {
                const QString& realName = idx.data(ROLE_PARAM_NAME).toString();
                const QString& displayName = realName;  //todo: mapping name.
                PARAM_CONTROL ctrl = (PARAM_CONTROL)idx.data(ROLE_PARAM_CTRL).toInt();
                VParamItem* paramItem = new VParamItem(VPARAM_PARAM, displayName);
                paramItem->m_info.control = ctrl;
                paramItem->m_index = idx;
                pItem->appendRow(paramItem);
                break;
            }
        }
    }
    else if (cls == PARAM_OUTPUT)
    {
        QList<QStandardItem*> lst = findItems("Out Sockets", Qt::MatchRecursive | Qt::MatchExactly);
        for (QStandardItem* pItem : lst)
        {
            if (pItem->data(ROLE_VPARAM_TYPE) == VPARAM_GROUP)
            {
                const QString& realName = idx.data(ROLE_PARAM_NAME).toString();
                const QString& displayName = realName;  //todo: mapping name.
                PARAM_CONTROL ctrl = (PARAM_CONTROL)idx.data(ROLE_PARAM_CTRL).toInt();
                VParamItem* paramItem = new VParamItem(VPARAM_PARAM, displayName);
                paramItem->m_info.control = ctrl;
                paramItem->m_index = idx;
                pItem->appendRow(paramItem);
                break;
            }
        }
    }
}

void ViewParamModel::onParamsAboutToBeRemoved(const QModelIndex& parent, int first, int last)
{
    IParamModel* pModel = qobject_cast<IParamModel*>(sender());
    ZASSERT_EXIT(pModel);
    const QModelIndex& idx = pModel->index(first, 0, parent);
    if (!idx.isValid()) return;
}

void ViewParamModel::clone(ViewParamModel* pModel)
{
    QStandardItem* pRoot = invisibleRootItem();
    ZASSERT_EXIT(pRoot);

    VParamItem* pLeft = static_cast<VParamItem*>(pRoot->child(0));
    VParamItem* pRight = static_cast<VParamItem*>(pModel->invisibleRootItem()->child(0));
    pLeft->cloneFrom(pRight);
}
