#include "curvegrid.h"
#include "curvenodeitem.h"
#include "curvemapview.h"
#include "curvesitem.h"
#include "../model/curvemodel.h"


CurvesItem::CurvesItem(CurveMapView* pView, CurveGrid* grid, const QRectF& rc, QGraphicsItem* parent)
    : QGraphicsObject(parent)
    , m_view(pView)
    , m_grid(grid)
    , m_model(nullptr)
{
}

CurvesItem::~CurvesItem()
{
    //todo: delete all nodes and curves.
}

void CurvesItem::initCurves(CurveModel* model)
{
    m_model = model;

    for (int r = 0; r < m_model->rowCount(); r++)
    {
        QModelIndex idx = m_model->index(r, 0);
        QPointF logicPos = m_model->data(idx, ROLE_NODEPOS).toPointF();
        QPointF left = m_model->data(idx, ROLE_LEFTPOS).toPointF();
        QPointF right = m_model->data(idx, ROLE_RIGHTPOS).toPointF();

        QPointF scenePos = m_grid->logicToScene(logicPos);
        QPointF leftScenePos = m_grid->logicToScene(logicPos + left);
        QPointF rightScenePos = m_grid->logicToScene(logicPos + right);
		QPointF leftOffset = leftScenePos - scenePos;
		QPointF rightOffset = rightScenePos - scenePos;

		CurveNodeItem* pNodeItem = new CurveNodeItem(idx, m_view, scenePos, m_grid, this);
		pNodeItem->initHandles(leftOffset, rightOffset);
		connect(pNodeItem, SIGNAL(geometryChanged()), this, SLOT(onNodeGeometryChanged()));
        connect(pNodeItem, SIGNAL(deleteTriggered()), this, SLOT(onNodeDeleted()));

		if (r == 0)
		{
            m_vecNodes.append(pNodeItem);
			continue;
		}

		CurvePathItem* pathItem = new CurvePathItem(this);
        connect(pathItem, SIGNAL(clicked(const QPointF&)), this, SLOT(onPathClicked(const QPointF&)));

		QPainterPath path;

        idx = m_model->index(r - 1, 0);
        logicPos = m_model->data(idx, ROLE_NODEPOS).toPointF();
        right = m_model->data(idx, ROLE_RIGHTPOS).toPointF();
		QPointF lastNodePos = m_grid->logicToScene(logicPos);
        QPointF lastRightPos = m_grid->logicToScene(logicPos + right);

		path.moveTo(lastNodePos);
		path.cubicTo(lastRightPos, leftScenePos, scenePos);
		pathItem->setPath(path);
        pathItem->update();

		m_vecNodes.append(pNodeItem);
        m_vecCurves.append(pathItem);
    }

    connect(m_model, &CurveModel::dataChanged, this, &CurvesItem::onDataChanged);
}

void CurvesItem::onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight, const QVector<int> &roles)
{
    int r = topLeft.row();
    Q_ASSERT(r >= 0 && r < m_vecNodes.size());
    CurveNodeItem *pNode = m_vecNodes[r];

    QGraphicsPathItem* pLeftCurve = r > 0 ? m_vecCurves[r - 1] : nullptr;
    if (pLeftCurve)
	{
        CurveNodeItem *pLeftNode = m_vecNodes[r - 1];
        QPainterPath path;
        path.moveTo(pLeftNode->pos());
        path.cubicTo(pLeftNode->rightHandlePos(), pNode->leftHandlePos(), pNode->pos());
        pLeftCurve->setPath(path);
        pLeftCurve->update();
	}

	QGraphicsPathItem* pRightCurve = (r < m_vecNodes.size() - 1) ? m_vecCurves[r] : nullptr;
    if (pRightCurve)
	{
        CurveNodeItem* pRightNode = m_vecNodes[r + 1];
        QPainterPath path;
        path.moveTo(pNode->pos());
        path.cubicTo(pNode->rightHandlePos(), pRightNode->leftHandlePos(), pRightNode->pos());
        pRightCurve->setPath(path);
        pRightCurve->update();
	}
}


int CurvesItem::indexOf(CurveNodeItem *pItem) const
{
    return m_vecNodes.indexOf(pItem);
}

int CurvesItem::nodeCount() const
{
    return m_vecNodes.size();
}

QPointF CurvesItem::nodePos(int i) const
{
    Q_ASSERT(i >= 0 && i < m_vecNodes.size());
    return m_vecNodes[i]->pos();
}

CurveNodeItem* CurvesItem::nodeItem(int i) const
{
    Q_ASSERT(i >= 0 && i < m_vecNodes.size());
    return m_vecNodes[i];
}

CurveModel* CurvesItem::model() const
{
    return m_model;
}

QRectF CurvesItem::boundingRect() const
{
    return childrenBoundingRect();
}

void CurvesItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget)
{
}

void CurvesItem::onNodeGeometryChanged()
{
    CurveNodeItem* pNode = qobject_cast<CurveNodeItem*>(sender());
    int i = m_vecNodes.indexOf(pNode);
    Q_ASSERT(i >= 0);

    QGraphicsPathItem* pLeftCurve = i > 0 ? m_vecCurves[i-1] : nullptr;
    if (pLeftCurve)
	{
        CurveNodeItem *pLeftNode = m_vecNodes[i - 1];
        QPainterPath path;
        path.moveTo(pLeftNode->pos());
        path.cubicTo(pLeftNode->rightHandlePos(), pNode->leftHandlePos(), pNode->pos());
        pLeftCurve->setPath(path);
        pLeftCurve->update();
	}

	QGraphicsPathItem* pRightCurve = (i < m_vecNodes.size() - 1) ? m_vecCurves[i] : nullptr;
    if (pRightCurve)
	{
        CurveNodeItem* pRightNode = m_vecNodes[i + 1];
        QPainterPath path;
        path.moveTo(pNode->pos());
        path.cubicTo(pNode->rightHandlePos(), pRightNode->leftHandlePos(), pRightNode->pos());
        pRightCurve->setPath(path);
        pRightCurve->update();
	}

    emit nodesDataChanged();
}

void CurvesItem::onNodeDeleted()
{
    CurveNodeItem* pItem = qobject_cast<CurveNodeItem*>(sender());
    Q_ASSERT(pItem);
    int i = m_vecNodes.indexOf(pItem);
    if (i == 0 || i == m_vecNodes.size() - 1)
        return;

	CurveNodeItem* pLeftNode = m_vecNodes[i - 1];
    CurveNodeItem* pRightNode = m_vecNodes[i + 1];

	//curves[i-1] as a new curve from node i-1 to node i.
	CurvePathItem* pathItem = m_vecCurves[i - 1];

    m_vecCurves[i]->deleteLater();
	pItem->deleteLater();

	CurvePathItem* pDeleleCurve = m_vecCurves[i];
	m_vecCurves.remove(i);
    m_vecNodes.remove(i);

	QPainterPath path;
    path.moveTo(pLeftNode->pos());
	path.cubicTo(pLeftNode->rightHandlePos(), pRightNode->leftHandlePos(), pRightNode->pos());
    pathItem->setPath(path);
	pathItem->update();

	emit nodesDataChanged();
}

void CurvesItem::onPathClicked(const QPointF& pos)
{
    CurvePathItem* pItem = qobject_cast<CurvePathItem*>(sender());
    Q_ASSERT(pItem);
    int i = m_vecCurves.indexOf(pItem);
    CurveNodeItem *pLeftNode = m_vecNodes[i];
    CurveNodeItem *pRightNode = m_vecNodes[i + 1];

	QPointF leftNodePos = pLeftNode->pos(), rightHdlPos = pLeftNode->rightHandlePos(),
			leftHdlPos = pRightNode->leftHandlePos(), rightNodePos = pRightNode->pos();

	/*
	QBezier bezier = QBezier::fromPoints(leftNodePos, rightHdlPos, leftHdlPos, rightNodePos);
	qreal t = (pos.x() - leftNodePos.x()) / (rightNodePos.x() - leftNodePos.x());
	QPointF k = bezier.derivedAt(t);
    QVector2D vec(k);
    vec.normalize();
	*/

	QPointF leftOffset(-50, 0);
    QPointF rightOffset(50, 0);

	//insert a new node.
    CurveNodeItem* pNewNode = new CurveNodeItem(QModelIndex(), m_view, pos, m_grid, this);
	connect(pNewNode, SIGNAL(geometryChanged()), this, SLOT(onNodeGeometryChanged()));
    connect(pNewNode, SIGNAL(deleteTriggered()), this, SLOT(onNodeDeleted()));

    pNewNode->initHandles(leftOffset, rightOffset);

	CurvePathItem* pLeftHalf = pItem;
    CurvePathItem* pRightHalf = new CurvePathItem(this);
	connect(pRightHalf, SIGNAL(clicked(const QPointF &)), this, SLOT(onPathClicked(const QPointF&)));

	QPainterPath leftPath;
    leftPath.moveTo(leftNodePos);
	leftPath.cubicTo(rightHdlPos, pNewNode->leftHandlePos(), pNewNode->pos());
    pLeftHalf->setPath(leftPath);
	pLeftHalf->update();

	QPainterPath rightPath;
    rightPath.moveTo(pNewNode->pos());
	rightPath.cubicTo(pNewNode->rightHandlePos(), leftHdlPos, rightNodePos);
    pRightHalf->setPath(rightPath);
	pRightHalf->update();

	m_vecNodes.insert(i + 1, pNewNode);
    m_vecCurves.insert(i + 1, pRightHalf);
}