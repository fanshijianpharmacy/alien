#include <gtest/gtest.h>

#include <QEventLoop>

#include "Base/ServiceLocator.h"
#include "Base/GlobalFactory.h"
#include "Base/NumberGenerator.h"
#include "ModelBasic/ModelBasicBuilderFacade.h"
#include "ModelBasic/Settings.h"
#include "ModelBasic/SimulationController.h"
#include "ModelBasic/DescriptionHelper.h"
#include "ModelBasic/SimulationParameters.h"
#include "ModelBasic/SpaceProperties.h"
#include "ModelBasic/SimulationAccess.h"
#include "ModelBasic/SimulationContext.h"

#include "ModelGpu/SimulationControllerGpu.h"
#include "ModelGpu/SimulationAccessGpu.h"
#include "ModelGpu/ModelGpuData.h"
#include "ModelGpu/ModelGpuBuilderFacade.h"

#include "Tests/Predicates.h"

#include "IntegrationTestHelper.h"
#include "IntegrationTestFramework.h"

class SimulationGpuTest
	: public IntegrationTestFramework
{
public:
	SimulationGpuTest();
	~SimulationGpuTest();

protected:
	void checkEnergy(DataDescription const& origData, DataDescription const& newData) const;
	void checkKineticEnergy(DataDescription const& origData, DataDescription const& newData) const;
	Physics::Velocities calcVelocitiesOfClusterPart(ClusterDescription const& cluster, set<uint64_t> const& cellIds) const;
	double calcEnergy(DataDescription const& data) const;
	double calcEnergy(ClusterDescription const& cluster) const;
	double calcKineticEnergy(DataDescription const& data) const;
	double calcKineticEnergy(ClusterDescription const& cluster) const;

protected:
	double const NearlyZero = FLOATINGPOINT_MEDIUM_PRECISION;

	SimulationControllerGpu* _controller = nullptr;
	SimulationContext* _context = nullptr;
	SpaceProperties* _spaceProp = nullptr;
	SimulationAccessGpu* _access = nullptr;
};

SimulationGpuTest::SimulationGpuTest()
	: IntegrationTestFramework({ 600, 300 })
{
	_controller = _gpuFacade->buildSimulationController({ _universeSize, _symbols, _parameters }, ModelGpuData(), 0);
	_context = _controller->getContext();
	_spaceProp = _context->getSpaceProperties();
	_access = _gpuFacade->buildSimulationAccess();
	_parameters = _context->getSimulationParameters();
	_numberGen = _context->getNumberGenerator();
	_access->init(_controller);
}

SimulationGpuTest::~SimulationGpuTest()
{
	delete _access;
	delete _controller;
}

void SimulationGpuTest::checkEnergy(DataDescription const& origData, DataDescription const& newData) const
{
	auto energyBefore = calcEnergy(origData);
	auto energyAfter = calcEnergy(newData);

	EXPECT_TRUE(isCompatible(energyBefore, energyAfter));
}

void SimulationGpuTest::checkKineticEnergy(DataDescription const & origData, DataDescription const & newData) const
{
	auto energyBefore = calcKineticEnergy(origData);
	auto energyAfter = calcKineticEnergy(newData);

	EXPECT_TRUE(isCompatible(energyBefore, energyAfter));
}

Physics::Velocities SimulationGpuTest::calcVelocitiesOfClusterPart(ClusterDescription const& cluster, set<uint64_t> const& cellIds) const
{
	CHECK(!cellIds.empty());
	vector<QVector2D> relPositionOfMasses;
	for(CellDescription const& cell : *cluster.cells) {
		if (cellIds.find(cell.id) != cellIds.end()) {
			relPositionOfMasses.emplace_back(*cell.pos - *cluster.pos);
		}
	}
	return Physics::velocitiesOfCenter({ *cluster.vel, *cluster.angularVel }, relPositionOfMasses);
}

double SimulationGpuTest::calcEnergy(DataDescription const & data) const
{
	auto result = 0.0;
	if (data.clusters) {
		for (auto const& cluster : *data.clusters) {
			result += calcEnergy(cluster);
		}
	}
	if (data.particles) {
		for (auto const& particle : *data.particles) {
			result += *particle.energy;
		}
	}

	return result;
}

double SimulationGpuTest::calcEnergy(ClusterDescription const & cluster) const
{
	auto result = calcKineticEnergy(cluster);
	if (cluster.cells) {
		for (CellDescription const& cell : *cluster.cells) {
			result += *cell.energy;
		}
	}
	return result;
}

double SimulationGpuTest::calcKineticEnergy(DataDescription const & data) const
{
	auto result = 0.0;
	if (data.clusters) {
		for (auto const& cluster : *data.clusters) {
			result += calcKineticEnergy(cluster);
		}
	}
	return result;
}

double SimulationGpuTest::calcKineticEnergy(ClusterDescription const& cluster) const
{
	auto mass = cluster.cells->size();
	auto vel = *cluster.vel;

	vector<QVector2D> relPositions;
	std::transform(cluster.cells->begin(), cluster.cells->end(), std::inserter(relPositions, relPositions.begin()),
		[&cluster](CellDescription const& cell) { return *cell.pos - *cluster.pos; });

	auto angularMass = Physics::angularMass(relPositions);
	auto angularVel = *cluster.angularVel;
	return Physics::kineticEnergy(mass, vel, angularMass, angularVel);
}

/**
* Situation: horizontal collision of two cells where both move such that no pixel overlapping occurs
* Expected result: direction of movement of both cells changed
*/
TEST_F(SimulationGpuTest, DISABLED_testCollisionOfSingleCells_horizontal_noPixelOverlapping)
{
	DataDescription origData;
	auto cellEnergy = _parameters->cellFunctionConstructorOffspringCellEnergy;

	auto cellId1 = _numberGen->getId();
	auto cell1 = CellDescription().setId(cellId1).setPos({ 100, 100 }).setMaxConnections(0).setEnergy(cellEnergy);
	auto cluster1 = ClusterDescription().setId(_numberGen->getId()).setVel({ 0.1f, 0 }).setAngle(0).setAngularVel(0)
		.addCell(cell1);
	cluster1.setPos(cluster1.getClusterPosFromCells());
	origData.addCluster(cluster1);

	auto cellId2 = _numberGen->getId();
	auto cell2 = CellDescription().setId(cellId2).setPos({ 110, 100 }).setMaxConnections(0).setEnergy(cellEnergy);
	auto cluster2 = ClusterDescription().setId(_numberGen->getId()).setVel({ -0.1f, 0 }).setAngle(0).setAngularVel(0)
		.addCell(cell2);
	cluster2.setPos(cluster2.getClusterPosFromCells());
	origData.addCluster(cluster2);

	IntegrationTestHelper::updateData(_access, origData);
	IntegrationTestHelper::runSimulation(150, _controller);

	IntRect rect = { { 0, 0 },{ _universeSize.x, _universeSize.y } };
	DataDescription newData = IntegrationTestHelper::getContent(_access, rect);

	ASSERT_EQ(2, newData.clusters->size());
	auto cellById = IntegrationTestHelper::getCellByCellId(newData);
	auto newCell1 = cellById.at(cellId1);
	auto newCell2 = cellById.at(cellId2);
	auto clusterById = IntegrationTestHelper::getClusterByCellId(newData);
	auto newCluster1 = clusterById.at(cellId1);
	auto newCluster2 = clusterById.at(cellId2);

	EXPECT_GE(99, newCell1.pos->x());
	EXPECT_TRUE(isCompatible(100.0f, newCell1.pos->y()));
	EXPECT_TRUE(isCompatible(QVector2D(-0.1f, 0), *newCluster1.vel));

	EXPECT_LE(111, newCell2.pos->x());
	EXPECT_TRUE(isCompatible(100.0f, newCell2.pos->y()));
	EXPECT_TRUE(isCompatible(QVector2D(0.1f, 0), *newCluster2.vel));
}


/**
* Situation: horizontal collision of two cells
* Expected result: direction of movement of both cells changed
*/
TEST_F(SimulationGpuTest, testCollisionOfSingleCells_horizontal)
{
	DataDescription origData;
	auto cellEnergy = _parameters->cellFunctionConstructorOffspringCellEnergy;

	auto cellId1 = _numberGen->getId();
	auto cell1 = CellDescription().setId(cellId1).setPos({ 100, 100 }).setMaxConnections(0).setEnergy(cellEnergy);
	auto cluster1 = ClusterDescription().setId(_numberGen->getId()).setVel({ 0.1f, 0 }).setAngle(0).setAngularVel(0)
		.addCell(cell1);
	cluster1.setPos(cluster1.getClusterPosFromCells());
	origData.addCluster(cluster1);

	auto cellId2 = _numberGen->getId();
	auto cell2 = CellDescription().setId(cellId2).setPos({ 110.2f, 100 }).setMaxConnections(0).setEnergy(cellEnergy);
	auto cluster2 = ClusterDescription().setId(_numberGen->getId()).setVel({ -0.1f, 0 }).setAngle(0).setAngularVel(0)
		.addCell(cell2);
	cluster2.setPos(cluster2.getClusterPosFromCells());
	origData.addCluster(cluster2);

	IntegrationTestHelper::updateData(_access, origData);
	IntegrationTestHelper::runSimulation(150, _controller);

	IntRect rect = { { 0, 0 },{ _universeSize.x, _universeSize.y } };
	DataDescription newData = IntegrationTestHelper::getContent(_access, rect);

	ASSERT_EQ(2, newData.clusters->size());
	auto cellById = IntegrationTestHelper::getCellByCellId(newData);
	auto newCell1 = cellById.at(cellId1);
	auto newCell2 = cellById.at(cellId2);
	auto clusterById = IntegrationTestHelper::getClusterByCellId(newData);
	auto newCluster1 = clusterById.at(cellId1);
	auto newCluster2 = clusterById.at(cellId2);

	EXPECT_GE(99, newCell1.pos->x());
	EXPECT_TRUE(isCompatible(100.0f, newCell1.pos->y()));
	EXPECT_TRUE(isCompatible(QVector2D(-0.1f, 0), *newCluster1.vel));

	EXPECT_LE(111, newCell2.pos->x());
	EXPECT_TRUE(isCompatible(100.0f, newCell2.pos->y()));
	EXPECT_TRUE(isCompatible(QVector2D(0.1f, 0), *newCluster2.vel));
}

/**
* Situation: vertical collision of two cells
* Expected result: direction of movement of both cells changed
*/
TEST_F(SimulationGpuTest, testCollisionOfSingleCells_vertical)
{
	DataDescription origData;
	auto cellEnergy = _parameters->cellFunctionConstructorOffspringCellEnergy;

	auto cellId1 = _numberGen->getId();
	auto cell1 = CellDescription().setId(cellId1).setPos({ 100, 100 }).setMaxConnections(0).setEnergy(cellEnergy);
	auto cluster1 = ClusterDescription().setId(_numberGen->getId()).setVel({ 0, 0.1f }).setAngle(0).setAngularVel(0)
		.addCell(cell1);
	cluster1.setPos(cluster1.getClusterPosFromCells());
	origData.addCluster(cluster1);

	auto cellId2 = _numberGen->getId();
	auto cell2 = CellDescription().setId(cellId2).setPos({ 100, 110.2f }).setMaxConnections(0).setEnergy(cellEnergy);
	auto cluster2 = ClusterDescription().setId(_numberGen->getId()).setVel({ 0, -0.1f }).setAngle(0).setAngularVel(0)
		.addCell(cell2);
	cluster2.setPos(cluster2.getClusterPosFromCells());
	origData.addCluster(cluster2);

	IntegrationTestHelper::updateData(_access, origData);
	IntegrationTestHelper::runSimulation(150, _controller);

	IntRect rect = { { 0, 0 },{ _universeSize.x, _universeSize.y } };
	DataDescription newData = IntegrationTestHelper::getContent(_access, rect);

	ASSERT_EQ(2, newData.clusters->size());
	auto cellById = IntegrationTestHelper::getCellByCellId(newData);
	auto newCell1 = cellById.at(cellId1);
	auto newCell2 = cellById.at(cellId2);
	auto clusterById = IntegrationTestHelper::getClusterByCellId(newData);
	auto newCluster1 = clusterById.at(cellId1);
	auto newCluster2 = clusterById.at(cellId2);

	EXPECT_GE(99, newCell1.pos->y());
	EXPECT_TRUE(isCompatible(100.0f, newCell1.pos->x()));
	EXPECT_TRUE(isCompatible(QVector2D(0, -0.1f), *newCluster1.vel));

	EXPECT_LE(111, newCell2.pos->y());
	EXPECT_TRUE(isCompatible(100.0f, newCell2.pos->x()));
	EXPECT_TRUE(isCompatible(QVector2D(0, 0.1f), *newCluster2.vel));
}

/**
* Situation:
*	- center collision of two horizontal cell clusters
*	- first cluster has no velocity while second cluster moves upward
* Expected result: first cluster moves upward while second cluster stand stills
*/
TEST_F(SimulationGpuTest, testCenterCollisionOfParallelLineClusters)
{
	DataDescription origData;
	origData.addCluster(createHorizontalCluster(100, QVector2D{ 100, 100 }, QVector2D{ 0, 0 }, 0));
	origData.addCluster(createHorizontalCluster(100, QVector2D{ 100, 110 }, QVector2D{ 0, -0.1f }, 0));
	uint64_t clusterId1 = origData.clusters->at(0).id;
	uint64_t clusterId2 = origData.clusters->at(1).id;

	IntegrationTestHelper::updateData(_access, origData);
	IntegrationTestHelper::runSimulation(150, _controller);

	IntRect rect = { { 0, 0 },{ _universeSize.x, _universeSize.y } };
	DataDescription newData = IntegrationTestHelper::getContent(_access, rect);
	ASSERT_EQ(2, newData.clusters->size());

	auto clusterById = IntegrationTestHelper::getClusterByClusterId(newData);
	{
		auto cluster = clusterById.at(clusterId1);
		EXPECT_EQ(100, cluster.pos->x());
		EXPECT_GE(100, cluster.pos->y());
		EXPECT_TRUE(isCompatible(0.0f, cluster.vel->x()));
		EXPECT_TRUE(isCompatible(-0.1f, cluster.vel->y()));
	}

	{
		auto cluster = clusterById.at(clusterId2);
		EXPECT_EQ(100, cluster.pos->x());
		EXPECT_LE(100, cluster.pos->y());
		EXPECT_TRUE(isCompatible(QVector2D(0, 0), *cluster.vel));
	}

	checkKineticEnergy(origData, newData);
}

/**
* Situation: horizontal collision of two line clusters at boundary
* Expected result: direction of movement of both cells changed accordingly
*/
TEST_F(SimulationGpuTest, testHorizontalCenterCollisionOfParallelLineClusters_atUniverseBoundary)
{
	DataDescription origData;
	auto size = _spaceProp->getSize();
	origData.addCluster(createHorizontalCluster(10, QVector2D{ size.x - 5.0f, 100 }, QVector2D{ 0.1f, 0 }, 0));
	origData.addCluster(createHorizontalCluster(10, QVector2D{ size.x + 5.0f, 100 }, QVector2D{ -0.1f,0 }, 0));
	uint64_t clusterId1 = origData.clusters->at(0).id;
	uint64_t clusterId2 = origData.clusters->at(1).id;

	IntegrationTestHelper::updateData(_access, origData);
	IntegrationTestHelper::runSimulation(20, _controller);

	IntRect rect = { { 0, 0 },{ _universeSize.x, _universeSize.y } };
	DataDescription newData = IntegrationTestHelper::getContent(_access, rect);

	ASSERT_EQ(2, newData.clusters->size());

	auto clusterById = IntegrationTestHelper::getClusterByClusterId(newData);
	{
		auto cluster = clusterById.at(clusterId1);
		EXPECT_TRUE(isCompatible(-0.1f, cluster.vel->x()));
		EXPECT_TRUE(isCompatible(0.0f, cluster.vel->y()));
		EXPECT_TRUE(isCompatible(0.0, *cluster.angularVel));
	}

	{
		auto cluster = clusterById.at(clusterId2);
		EXPECT_TRUE(isCompatible(0.1f, cluster.vel->x()));
		EXPECT_TRUE(isCompatible(0.0f, cluster.vel->y()));
		EXPECT_TRUE(isCompatible(0.0, *cluster.angularVel));
	}
}


/**
* Situation: vertical collision of two line clusters at boundary
* Expected result: direction of movement of both cells changed accordingly
*/
TEST_F(SimulationGpuTest, testVerticalCenterCollisionOfParallelLineClusters_atUniverseBoundary)
{
	auto size = _spaceProp->getSize();

	DataDescription origData;
	origData.addCluster(createHorizontalCluster(10, QVector2D{ 100, size.y + 0.5f }, QVector2D{ 0, -0.1f }, 0.0));
	origData.addCluster(createHorizontalCluster(10, QVector2D{ 100, size.y - 0.5f }, QVector2D{ 0, 0.1f }, 0.0));
	uint64_t clusterId1 = origData.clusters->at(0).id;
	uint64_t clusterId2 = origData.clusters->at(1).id;

	IntegrationTestHelper::updateData(_access, origData);
	IntegrationTestHelper::runSimulation(1, _controller);

	IntRect rect = { { 0, 0 },{ _universeSize.x, _universeSize.y } };
	DataDescription newData = IntegrationTestHelper::getContent(_access, rect);
	ASSERT_EQ(2, newData.clusters->size());

	auto clusterById = IntegrationTestHelper::getClusterByClusterId(newData);
	{
		auto cluster = clusterById.at(clusterId1);
		EXPECT_TRUE(isCompatible(QVector2D(0.0f, 0.1f), *cluster.vel));
		EXPECT_TRUE(isCompatible(0.0, *cluster.angularVel));
	}

	{
		auto cluster = clusterById.at(clusterId2);
		EXPECT_TRUE(isCompatible(QVector2D(0.0f, -0.1f), *cluster.vel));
		EXPECT_TRUE(isCompatible(0.0, *cluster.angularVel));
	}

	checkKineticEnergy(origData, newData);
}


/**
* Situation:
*	- sidewise collision of two horizontal cell clusters
*	- first cluster has no velocity while second cluster moves upward
* Expected result: both clusters move upwards and rotate counterclockwise
*/
TEST_F(SimulationGpuTest, testSidewiseCollisionOfParallelLineClusters)
{
	DataDescription origData;
	origData.addCluster(createHorizontalCluster(100, QVector2D{ 100, 100 }, QVector2D{ 0, 0 }, 0));
	origData.addCluster(createHorizontalCluster(100, QVector2D{ 199, 110 }, QVector2D{ 0, -0.1f }, 0));
	uint64_t clusterId1 = origData.clusters->at(0).id;
	uint64_t clusterId2 = origData.clusters->at(1).id;

	IntegrationTestHelper::updateData(_access, origData);
	IntegrationTestHelper::runSimulation(120, _controller);

	IntRect rect = { { 0, 0 },{ _universeSize.x, _universeSize.y } };
	DataDescription newData = IntegrationTestHelper::getContent(_access, rect);
	ASSERT_EQ(2, newData.clusters->size());

	auto clusterById = IntegrationTestHelper::getClusterByClusterId(newData);
	{
		auto cluster = clusterById.at(clusterId1);
		EXPECT_GE(NearlyZero, cluster.vel->x());
		EXPECT_GE(-NearlyZero, cluster.vel->y());
		EXPECT_GE(-NearlyZero, *cluster.angularVel);
	}

	{
		auto cluster = clusterById.at(clusterId2);
		EXPECT_LE(-NearlyZero, cluster.vel->x());
		EXPECT_GE(NearlyZero, cluster.vel->y());
		EXPECT_GE(-NearlyZero, *cluster.angularVel);
	}

	checkKineticEnergy(origData, newData);
}

/**
* Situation:
*	- sidewise collision of two rectangular cell clusters
*	- first cluster has no velocity while second cluster moves upward
* Expected result: both clusters move upwards and rotate counterclockwise
*/
TEST_F(SimulationGpuTest, testSidewiseCollisionOfRectangleClusters)
{
	DataDescription origData;
	origData.addCluster(createRectangleCluster({ 10, 10 }, QVector2D{ 100, 100 }, QVector2D{ 0, 0 }));
	origData.addCluster(createRectangleCluster({ 10, 10 }, QVector2D{ 105, 120 }, QVector2D{ 0, -0.5f }));
	uint64_t clusterId1 = origData.clusters->at(0).id;
	uint64_t clusterId2 = origData.clusters->at(1).id;

	IntegrationTestHelper::updateData(_access, origData);
	IntegrationTestHelper::runSimulation(23, _controller);

	IntRect rect = { { 0, 0 },{ _universeSize.x, _universeSize.y } };
	DataDescription newData = IntegrationTestHelper::getContent(_access, rect);
	ASSERT_EQ(2, newData.clusters->size());

	auto clusterById = IntegrationTestHelper::getClusterByClusterId(newData);
	{
		auto cluster = clusterById.at(clusterId1);
		EXPECT_GE(-NearlyZero, cluster.vel->y());
		EXPECT_GE(-NearlyZero, *cluster.angularVel);
	}

	{
		auto cluster = clusterById.at(clusterId2);
		EXPECT_GE(-NearlyZero, cluster.vel->y());
		EXPECT_GE(-NearlyZero, *cluster.angularVel);
	}

	checkKineticEnergy(origData, newData);
}

/**
* Situation:
*	- sidewise collision of two rectangular cell clusters
*	- both clusters have velocity and angular velocity
* Expected result: energy is conserved
*/
TEST_F(SimulationGpuTest, testSidewiseCollisionOfRectangleClusters_withAngularVelocities)
{
	DataDescription origData;
	auto cluster1 = createRectangleCluster({ 20, 20 }, QVector2D{ 457.46f, 356.37f }, QVector2D{ -0.011f, -0.077f });
	cluster1.angle = 1.409;
	cluster1.angularVel = -0.271;
	auto cluster2 = createRectangleCluster({ 20, 20 }, QVector2D{ 476.24f, 341.08f }, QVector2D{ -0.088f, 0.061f });
	cluster2.angle = 307.34;
	cluster2.angularVel = 0.394;
	origData.addCluster(cluster1);
	origData.addCluster(cluster2);
	uint64_t clusterId1 = cluster1.id;
	uint64_t clusterId2 = cluster2.id;

	IntegrationTestHelper::updateData(_access, origData);
	IntegrationTestHelper::runSimulation(1, _controller);

	IntRect rect = { { 0, 0 },{ _universeSize.x, _universeSize.y } };
	DataDescription newData = IntegrationTestHelper::getContent(_access, rect);
	ASSERT_EQ(2, newData.clusters->size());

	checkKineticEnergy(origData, newData);
}

/**
* Situation:
*	- sidewise collision of two orthogonal cell clusters
*	- first cluster has no velocity while second cluster moves upward
* Expected result:
*	- first cluster moves upward and rotate counterclockwise
*	- second cluster does not move on x axis and does not rotate
*/
TEST_F(SimulationGpuTest, testSidewiseCollisionOfOrthogonalLineClusters)
{
	DataDescription origData;
	origData.addCluster(createHorizontalCluster(100, QVector2D{ 100, 100 }, QVector2D{ 0, 0 }, 0));
	origData.addCluster(createVerticalCluster(100, QVector2D{ 148, 160 }, QVector2D{ 0, -0.1f }));
	uint64_t clusterId1 = origData.clusters->at(0).id;
	uint64_t clusterId2 = origData.clusters->at(1).id;

	IntegrationTestHelper::updateData(_access, origData);
	IntegrationTestHelper::runSimulation(120, _controller);

	IntRect rect = { { 0, 0 },{ _universeSize.x, _universeSize.y } };
	DataDescription newData = IntegrationTestHelper::getContent(_access, rect);
	ASSERT_EQ(2, newData.clusters->size());

	auto clusterById = IntegrationTestHelper::getClusterByClusterId(newData);
	{
		auto cluster = clusterById.at(clusterId1);
		EXPECT_GE(NearlyZero, cluster.vel->x());
		EXPECT_GE(-NearlyZero, cluster.vel->y());
		EXPECT_GE(-NearlyZero, *cluster.angularVel);
	}

	{
		auto cluster = clusterById.at(clusterId2);
		EXPECT_TRUE(isCompatible(0.0f, cluster.vel->x()));
		EXPECT_GE(NearlyZero, cluster.vel->y());
		EXPECT_TRUE(abs(*cluster.angularVel) < 0.01);
	}

	checkKineticEnergy(origData, newData);
}

/**
* Situation:
*	- sidewise collision of two traversal cell clusters
*	- first cluster is arranged horizontal and has no velocity
*	- second cluster is below the first one, sloped at 45 degree and moves upward
* Expected result:
*	- first cluster moves upward and rotate clockwise
*	- second cluster moves upward and rotate counterclockwise
*/
TEST_F(SimulationGpuTest, testSidewiseCollisionOfTraversalLineClusters)
{
	DataDescription origData;
	origData.addCluster(createHorizontalCluster(100, QVector2D{ 100, 100 }, QVector2D{ 0, 0 }, 0));
	origData.addCluster(createLineCluster(100, QVector2D{ 100, 145 }, QVector2D{ 0, -0.5f }, 45, 0));
	uint64_t clusterId1 = origData.clusters->at(0).id;
	uint64_t clusterId2 = origData.clusters->at(1).id;

	IntegrationTestHelper::updateData(_access, origData);
	IntegrationTestHelper::runSimulation(40, _controller);

	IntRect rect = { { 0, 0 },{ _universeSize.x, _universeSize.y } };
	DataDescription newData = IntegrationTestHelper::getContent(_access, rect);
	ASSERT_EQ(2, newData.clusters->size());

	auto clusterById = IntegrationTestHelper::getClusterByClusterId(newData);
	{
		auto cluster = clusterById.at(clusterId1);
		EXPECT_TRUE(abs(cluster.vel->x()) < NearlyZero);
		EXPECT_GE(-NearlyZero, cluster.vel->y());
		EXPECT_LE(NearlyZero, *cluster.angularVel);
	}

	{
		auto cluster = clusterById.at(clusterId2);
		EXPECT_TRUE(abs(cluster.vel->x()) < NearlyZero);
		EXPECT_GE(-NearlyZero, cluster.vel->y());
		EXPECT_GE(-NearlyZero, *cluster.angularVel);
	}

	checkKineticEnergy(origData, newData);
}

/**
* Situation:
*	- sidewise collision of two traversal cell clusters
*	- first cluster is arranged horizontal and has no velocity
*	- second cluster is below the first one, sloped at 45 degree and moves upward
*	- simulate until second collision occur
* Expected result:
*	- velocity of first cluster: x: positive, y: negative, angular vel: negative 
*	- velocity of second cluster: x: negative, y: negative, angular vel: positive
*/
TEST_F(SimulationGpuTest, testSidewiseCollisionOfTraversalLineClusters_waitUntilSecondCollision)
{
	DataDescription origData;
	origData.addCluster(createHorizontalCluster(100, QVector2D{ 100, 100 }, QVector2D{ 0, 0 }, 0));
	origData.addCluster(createLineCluster(100, QVector2D{ 100, 145 }, QVector2D{ 0, -0.5f }, 45, 0));
	uint64_t clusterId1 = origData.clusters->at(0).id;
	uint64_t clusterId2 = origData.clusters->at(1).id;

	IntegrationTestHelper::updateData(_access, origData);
	IntegrationTestHelper::runSimulation(100, _controller);

	IntRect rect = { { 0, 0 },{ _universeSize.x, _universeSize.y } };
	DataDescription newData = IntegrationTestHelper::getContent(_access, rect);
	ASSERT_EQ(2, newData.clusters->size());

	auto clusterById = IntegrationTestHelper::getClusterByClusterId(newData);
	{
		auto cluster = clusterById.at(clusterId1);
		EXPECT_LE(NearlyZero, cluster.vel->x());
		EXPECT_GE(-NearlyZero, cluster.vel->y());
		EXPECT_GE(-NearlyZero, *cluster.angularVel);
	}

	{
		auto cluster = clusterById.at(clusterId2);
		EXPECT_GE(-NearlyZero, cluster.vel->x());
		EXPECT_GE(-NearlyZero, cluster.vel->y());
		EXPECT_LE(NearlyZero, *cluster.angularVel);
	}

	checkKineticEnergy(origData, newData);
}

/**
* Situation: fusion of two particles
* Expected result: one particle remains with average velocity
*/
TEST_F(SimulationGpuTest, testFusionOfSingleParticles)
{
	DataDescription origData;
	auto particleEnergy = _parameters->cellMinEnergy / 2.0;

	auto particleId1 = _numberGen->getId();
	auto particle1 = ParticleDescription().setId(particleId1).setEnergy(particleEnergy).setPos({ 100, 100 }).setVel({ 0.5, 0.0 });
	origData.addParticle(particle1);

	auto particleId2 = _numberGen->getId();
	auto particle2 = ParticleDescription().setId(particleId1).setEnergy(particleEnergy).setPos({ 110, 100 }).setVel({ -0.5, 0.0 });
	origData.addParticle(particle2);

	IntegrationTestHelper::updateData(_access, origData);
	IntegrationTestHelper::runSimulation(30, _controller);

	IntRect rect = { { 0, 0 },{ _universeSize.x, _universeSize.y } };
	DataDescription newData = IntegrationTestHelper::getContent(_access, rect);

	ASSERT_FALSE(newData.clusters);
	ASSERT_EQ(1, newData.particles->size());
	auto newParticle = newData.particles->front();
	EXPECT_TRUE(isCompatible(QVector2D(0, 0), *newParticle.vel));
}

/**
* Situation: fusion of two particles
* Expected result: one particle remains with average velocity
*/
TEST_F(SimulationGpuTest, testFusionOfManyParticles)
{
	DataDescription origData;
	for (int i = 0; i < 100000; ++i) {
		origData.addParticle(createParticle());
	}

	IntegrationTestHelper::updateData(_access, origData);
	IntegrationTestHelper::runSimulation(300, _controller);
	DataDescription newData = IntegrationTestHelper::getContent(_access, { { 0, 0 },{ _universeSize.x, _universeSize.y } });

	checkEnergy(origData, newData);
}

/**
 * Situation: cluster with cross structure where middle cell connecting 4 parts has low energy            
 * Expected result: cluster decomposes into 4 parts
 */
TEST_F(SimulationGpuTest, testDecomposeClusterAfterLowEnergy)
{
	DataDescription origData;
	{
		auto cluster = ClusterDescription().setId(_numberGen->getId()).setVel({ 0, 0 }).setAngle(0).setAngularVel(0);
		for (int i = 0; i < 30; ++i) {
			auto cell = CellDescription().setId(_numberGen->getId()).setPos({ 100, 100 + float(i) }).setMaxConnections(4);
			if (15 == i) {
				cell.setEnergy(_parameters->cellMinEnergy / 2);
			}
			else {
				cell.setEnergy(_parameters->cellMinEnergy * 2);
			}
			cluster.addCell(cell);
		}
		auto leftCell = CellDescription().setId(_numberGen->getId()).setPos({ 99, 115 }).setMaxConnections(4).setEnergy(_parameters->cellMinEnergy * 2);
		cluster.addCell(leftCell);
		auto rightCell = CellDescription().setId(_numberGen->getId()).setPos({ 101, 115 }).setMaxConnections(4).setEnergy(_parameters->cellMinEnergy * 2);
		cluster.addCell(rightCell);

		for (int i = 0; i < 30; ++i) {
			list<uint64_t> connectingCells;
			if (i > 0) {
				connectingCells.emplace_back(cluster.cells->at(i - 1).id);
			}
			if (i < 30 - 1) {
				connectingCells.emplace_back(cluster.cells->at(i + 1).id);
			}
			cluster.cells->at(i).setConnectingCells(connectingCells);
		}
		cluster.cells->at(30).addConnection(cluster.cells->at(15).id);
		cluster.cells->at(15).addConnection(cluster.cells->at(30).id);
		cluster.cells->at(31).addConnection(cluster.cells->at(15).id);
		cluster.cells->at(15).addConnection(cluster.cells->at(31).id);

		cluster.setPos(cluster.getClusterPosFromCells());
		origData.addCluster(cluster);
	}

	IntegrationTestHelper::updateData(_access, origData);
	IntegrationTestHelper::runSimulation(3, _controller);

	DataDescription newData = IntegrationTestHelper::getContent(_access, { { 0, 0 },{ _universeSize.x, _universeSize.y } });

	auto numClusters = newData.clusters ? newData.clusters->size() : 0;
	ASSERT_EQ(4, numClusters);

	unordered_map<int, vector<ClusterDescription>> clustersBySize;
	for (ClusterDescription const& cluster : *newData.clusters) {
		int numCells = cluster.cells ? cluster.cells->size() : 0;
		clustersBySize[numCells].emplace_back(cluster);
	};
	ASSERT_LE(2, clustersBySize.at(1).size());
	ASSERT_EQ(1, clustersBySize.at(14).size());
	ASSERT_EQ(1, clustersBySize.at(15).size());

	unordered_map<uint64_t, CellDescription> origCellById = IntegrationTestHelper::getCellByCellId(origData);
	for (ClusterDescription const& cluster : *newData.clusters) {
		EXPECT_EQ(cluster.getClusterPosFromCells(), *cluster.pos);
		for (CellDescription const& cell : *cluster.cells) {
			CellDescription const& origCell = origCellById.at(cell.id);
			EXPECT_TRUE(isCompatible(cell.pos, origCell.pos));
		}
	}
}

/**
* Situation: cluster with line structure where middle cell has low energy
* Expected result: cluster decomposes into 2 parts
*/
TEST_F(SimulationGpuTest, testDecomposeClusterAfterLowEnergy_withDifferentAngleAndRotation)
{
	DataDescription origData;
	origData.addCluster(createHorizontalCluster(5, QVector2D{ 100, 100 }, QVector2D{ 0, 0 }, 1.0));
	origData.addCluster(createHorizontalCluster(5, QVector2D{ 200, 100 }, QVector2D{ 0, 0 }, 1.0));	//second cluster for comparison

	auto lowEnergy = _parameters->cellMinEnergy / 2.0;
	origData.clusters->at(0).cells->at(2).energy = lowEnergy;
	origData.clusters->at(0).angle = 90;

	IntegrationTestHelper::updateData(_access, origData);
	IntegrationTestHelper::runSimulation(1, _controller);
	DataDescription newData = IntegrationTestHelper::getContent(_access, { { 0, 0 },{ _universeSize.x, _universeSize.y } });

	auto newClusterById = IntegrationTestHelper::getClusterByCellId(newData);
	ClusterDescription refCluster = newClusterById.at(origData.clusters->at(1).cells->at(0).id);
	{
		ClusterDescription firstFragment = newClusterById.at(origData.clusters->at(0).cells->at(0).id);

		set<uint64_t> firstFragmentCellIds;
		for (int i = 0; i < 2; ++i) {
			firstFragmentCellIds.insert(origData.clusters->at(1).cells->at(i).id);
		}
		Physics::Velocities velocities = calcVelocitiesOfClusterPart(refCluster, firstFragmentCellIds);
		EXPECT_TRUE(isCompatible(velocities.linear, *firstFragment.vel));
		EXPECT_TRUE(isCompatible(velocities.angular, *firstFragment.angularVel));
	}
	{
		ClusterDescription secondFragment = newClusterById.at(origData.clusters->at(0).cells->at(4).id);

		set<uint64_t> secondFragmentCellIds;
		for (int i = 3; i < 5; ++i) {
			secondFragmentCellIds.insert(origData.clusters->at(1).cells->at(i).id);
		}
		Physics::Velocities velocities = calcVelocitiesOfClusterPart(refCluster, secondFragmentCellIds);
		EXPECT_TRUE(isCompatible(velocities.linear, *secondFragment.vel));
		EXPECT_TRUE(isCompatible(velocities.angular, *secondFragment.angularVel));
	}
}
