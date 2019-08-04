#include <boost/range/adaptors.hpp>

#include "Base/ServiceLocator.h"
#include "IntegrationGpuTestFramework.h"
#include "ModelBasic/CellComputerCompiler.h"
#include "ModelBasic/QuantityConverter.h"

class ConstructorGpuTests : public IntegrationGpuTestFramework
{
public:
    ConstructorGpuTests()
        : IntegrationGpuTestFramework()
    {}

    virtual ~ConstructorGpuTests() = default;

protected:
    virtual void SetUp();

    struct TestResult
    {
        QVector2D movementOfCenter;
        int increaseNumberOfCells;
        TokenDescription token;
        CellDescription constructorCell;
        optional<CellDescription> constructedCell;
    };
    struct ConstructionOnLineClusterTestParameters
    {
        MEMBER_DECLARATION(ConstructionOnLineClusterTestParameters, optional<float>, horizontalObstacleAt, boost::none);
        MEMBER_DECLARATION(ConstructionOnLineClusterTestParameters, TokenDescription, token, TokenDescription());
    };
    TestResult runConstructionOnLineClusterTest(ConstructionOnLineClusterTestParameters const& parameters);
    TestResult runConstructionOnWedgeClusterTest(TokenDescription const& token, float wedgeAngle, float clusterAngle)
        const;
    TestResult runConstructionOnTriangleClusterTest(TokenDescription const& token) const;

    struct TokenForConstructionParameters
    {
        MEMBER_DECLARATION(TokenForConstructionParameters, optional<float>, energy, boost::none);
        MEMBER_DECLARATION(
            TokenForConstructionParameters,
            Enums::ConstrIn::Type,
            constructionInput,
            Enums::ConstrIn::DO_NOTHING);
        MEMBER_DECLARATION(
            TokenForConstructionParameters,
            Enums::ConstrInOption::Type,
            constructionOption,
            Enums::ConstrInOption::STANDARD);
        MEMBER_DECLARATION(
            TokenForConstructionParameters,
            Enums::CellFunction::Type,
            cellFunctionType,
            Enums::CellFunction::COMPUTER);
        MEMBER_DECLARATION(TokenForConstructionParameters, int, cellBranchNumber, 0);
        MEMBER_DECLARATION(TokenForConstructionParameters, int, maxConnections, 0);
        MEMBER_DECLARATION(TokenForConstructionParameters, QByteArray, staticData, QByteArray());
        MEMBER_DECLARATION(TokenForConstructionParameters, QByteArray, mutableData, QByteArray());
        MEMBER_DECLARATION(TokenForConstructionParameters, float, angle, 0.0f);
        MEMBER_DECLARATION(TokenForConstructionParameters, float, distance, 0.0f);
    };
    TokenDescription createTokenForConstruction(TokenForConstructionParameters tokenParameters) const;

    struct Expectations
    {
        MEMBER_DECLARATION(Expectations, Enums::ConstrOut::Type, tokenOutput, Enums::ConstrOut::SUCCESS);
        MEMBER_DECLARATION(Expectations, optional<QVector2D>, constructedCellRelPos, boost::none);
        MEMBER_DECLARATION(Expectations, optional<TokenDescription>, constructedToken, boost::none);
        MEMBER_DECLARATION(Expectations, bool, destruction, false);
    };
    void checkResult(TestResult const& testResult, Expectations const& expectations) const;

protected:
    float _offspringDistance = 0.0f;
};


/************************************************************************/
/* Implementation                                                       */
/************************************************************************/

void ConstructorGpuTests::SetUp()
{
    _parameters.radiationProb = 0;  //exclude radiation
    _parameters.cellFunctionConstructorOffspringCellDistance = 1;
    _context->setSimulationParameters(_parameters);

    _offspringDistance = _parameters.cellFunctionConstructorOffspringCellDistance;
}

auto ConstructorGpuTests::runConstructionOnLineClusterTest(ConstructionOnLineClusterTestParameters const& parameters)
    -> TestResult
{
    DataDescription origData;
    auto cluster = createHorizontalCluster(2, QVector2D{10.5, 10.5}, QVector2D{}, 0);

    auto& firstCell = cluster.cells->at(0);
    firstCell.tokenBranchNumber = 0;
    firstCell.addToken(parameters._token);

    auto& secondCell = cluster.cells->at(1);
    secondCell.tokenBranchNumber = 1;
    secondCell.cellFeature = CellFeatureDescription().setType(Enums::CellFunction::CONSTRUCTOR);

    origData.addCluster(cluster);

    std::unordered_set<uint64_t> obstacleCellIds;

    if (parameters._horizontalObstacleAt) {
        auto const option = parameters._token.data->at(Enums::Constr::IN_OPTION);
        auto withSeparation = Enums::ConstrInOption::FINISH_WITH_SEP == option
            || Enums::ConstrInOption::FINISH_WITH_SEP_RED == option
            || Enums::ConstrInOption::FINISH_WITH_TOKEN_SEP_RED == option;
        auto const expectedOffspringCellRelPos =
            withSeparation ? QVector2D{_offspringDistance * 2 + 0.5f, 0} : QVector2D{_offspringDistance + 0.5f, 0};
        auto const expectedClusterRelCenterPos = expectedOffspringCellRelPos / 3;
        auto const expectedConstructorRelPos = QVector2D{ 0.5, 0 } - expectedClusterRelCenterPos;
        auto const expectedConstructorAbsPos = expectedConstructorRelPos + *cluster.pos;
        auto const obstacleCellAbsPos = expectedConstructorAbsPos + QVector2D{*parameters._horizontalObstacleAt, 0};

        QVector2D obstacleCenterPos;
        if (*parameters._horizontalObstacleAt > 0) {
            obstacleCenterPos = obstacleCellAbsPos + QVector2D{1.5f + _parameters.cellMinDistance / 2, 0};
        } else {
            obstacleCenterPos = obstacleCellAbsPos - QVector2D{1.5f + _parameters.cellMinDistance / 2, 0};
        }
        auto obstacle = createHorizontalCluster(4, obstacleCenterPos, QVector2D{}, 0);
        origData.addCluster(obstacle);
        for (auto const& cell : *obstacle.cells) {
            obstacleCellIds.insert(cell.id);
        }
    }

    IntegrationTestHelper::updateData(_access, origData);

    //perform test
    IntegrationTestHelper::runSimulation(1, _controller);

    //check results
    DataDescription newData = IntegrationTestHelper::getContent(_access, {{0, 0}, {_universeSize.x, _universeSize.y}});

    checkEnergy(origData, newData);

    auto newCellByCellId = IntegrationTestHelper::getCellByCellId(newData);

    std::unordered_map<uint64_t, CellDescription> newCellsWithoutObstacleByCellId;
    for (auto const& newCell : newCellByCellId | boost::adaptors::map_values) {
        if (obstacleCellIds.find(newCell.id) == obstacleCellIds.end()) {
            newCellsWithoutObstacleByCellId.insert_or_assign(newCell.id, newCell);
        }
    }

    QVector2D newCenter;
    for (auto const& newCell : newCellsWithoutObstacleByCellId | boost::adaptors::map_values) {
        newCenter += *newCell.pos;
    }
    newCenter /= newCellsWithoutObstacleByCellId.size();

    TestResult result;
    result.movementOfCenter = newCenter - *cluster.pos;
    result.increaseNumberOfCells = newCellsWithoutObstacleByCellId.size() - cluster.cells->size();

    auto const& newSecondCell = newCellByCellId.at(secondCell.id);
    auto const& newToken = newSecondCell.tokens->at(0);

    result.token = newToken;
    result.constructorCell = newSecondCell;

    std::list<CellDescription> remainingCells;
    for (auto const& newCell : newCellByCellId | boost::adaptors::map_values) {
        if (newCell.id != firstCell.id && newCell.id != secondCell.id
            && obstacleCellIds.find(newCell.id) == obstacleCellIds.end()) {
            remainingCells.push_back(newCell);
        }
    }
    EXPECT_GE(1, remainingCells.size());

    if (!remainingCells.empty()) {
        result.constructedCell = *remainingCells.begin();
    }

    return result;
}

auto ConstructorGpuTests::runConstructionOnWedgeClusterTest(
    TokenDescription const& token,
    float wedgeAngle,
    float clusterAngle) const -> TestResult
{
    ClusterDescription cluster;
    cluster.setId(_numberGen->getId()).setVel(QVector2D{}).setAngle(0).setAngularVel(0);

    auto const center = QVector2D{10.5f, 10.5f};
    auto const cellEnergy = _parameters.cellFunctionConstructorOffspringCellEnergy;
    auto const relPos1 = Physics::unitVectorOfAngle(clusterAngle + 270 + wedgeAngle / 2);
    auto const relPos2 = QVector2D{0, 0};
    auto const relPos3 = Physics::unitVectorOfAngle(clusterAngle + 270 - wedgeAngle / 2);
    auto const cellId1 = _numberGen->getId();
    auto const cellId2 = _numberGen->getId();
    auto const cellId3 = _numberGen->getId();
    cluster.addCells({CellDescription()
                          .setEnergy(cellEnergy)
                          .setPos(center + relPos1)
                          .setMaxConnections(1)
                          .setConnectingCells({cellId2})
                          .setTokenBranchNumber(0)
                          .setId(cellId1)
                          .setCellFeature(CellFeatureDescription())
                          .addToken(token),
                      CellDescription()
                          .setEnergy(cellEnergy)
                          .setPos(center + relPos2)
                          .setMaxConnections(2)
                          .setConnectingCells({cellId1, cellId3})
                          .setTokenBranchNumber(1)
                          .setId(cellId2)
                          .setCellFeature(CellFeatureDescription().setType(Enums::CellFunction::CONSTRUCTOR)),
                      CellDescription()
                          .setEnergy(cellEnergy)
                          .setPos(center + relPos3)
                          .setMaxConnections(1)
                          .setConnectingCells({cellId2})
                          .setTokenBranchNumber(2)
                          .setId(cellId3)
                          .setCellFeature(CellFeatureDescription())});

    cluster.setPos(cluster.getClusterPosFromCells());

    DataDescription origData;
    origData.addCluster(cluster);

    IntegrationTestHelper::updateData(_access, origData);

    //perform test
    IntegrationTestHelper::runSimulation(1, _controller);

    //check results
    DataDescription newData = IntegrationTestHelper::getContent(_access, {{0, 0}, {_universeSize.x, _universeSize.y}});
    checkEnergy(origData, newData);

    auto const& newCluster = newData.clusters->at(0);

    TestResult result;
    result.movementOfCenter = *newCluster.pos - *cluster.pos;
    result.increaseNumberOfCells = newCluster.cells->size() - cluster.cells->size();

    auto newCellByCellId = IntegrationTestHelper::getCellByCellId(newData);

    auto const& newCell2 = newCellByCellId.at(cellId2);
    auto const& newToken = newCell2.tokens->at(0);

    result.token = newToken;
    result.constructorCell = newCell2;

    newCellByCellId.erase(cellId1);
    newCellByCellId.erase(cellId2);
    newCellByCellId.erase(cellId3);
    if (!newCellByCellId.empty()) {
        result.constructedCell = newCellByCellId.begin()->second;
    }

    return result;
}

auto ConstructorGpuTests::runConstructionOnTriangleClusterTest(TokenDescription const& token) const -> TestResult
{
    ClusterDescription cluster;
    cluster.setId(_numberGen->getId()).setVel(QVector2D{}).setAngle(0).setAngularVel(0);

    auto const center = QVector2D{10, 10};
    auto const cellEnergy = _parameters.cellFunctionConstructorOffspringCellEnergy;
    auto const relPos1 = QVector2D{0, -1};
    auto const relPos2 = QVector2D{-1, 0};
    auto const relPos3 = QVector2D{0, 1};
    auto const relPos4 = QVector2D{0, 0};
    auto const cellId1 = _numberGen->getId();
    auto const cellId2 = _numberGen->getId();
    auto const cellId3 = _numberGen->getId();
    auto const cellId4 = _numberGen->getId();
    cluster.addCells({CellDescription()
                          .setEnergy(cellEnergy)
                          .setPos(center + relPos1)
                          .setMaxConnections(1)
                          .setConnectingCells({cellId4})
                          .setTokenBranchNumber(0)
                          .setId(cellId1)
                          .setCellFeature(CellFeatureDescription())
                          .addToken(token),
                      CellDescription()
                          .setEnergy(cellEnergy)
                          .setPos(center + relPos2)
                          .setMaxConnections(1)
                          .setConnectingCells({cellId4})
                          .setTokenBranchNumber(0)
                          .setId(cellId2)
                          .setCellFeature(CellFeatureDescription()),
                      CellDescription()
                          .setEnergy(cellEnergy)
                          .setPos(center + relPos3)
                          .setMaxConnections(1)
                          .setConnectingCells({cellId4})
                          .setTokenBranchNumber(0)
                          .setId(cellId3)
                          .setCellFeature(CellFeatureDescription()),
                      CellDescription()
                          .setEnergy(cellEnergy)
                          .setPos(center + relPos4)
                          .setMaxConnections(3)
                          .setConnectingCells({cellId1, cellId2, cellId3})
                          .setTokenBranchNumber(1)
                          .setId(cellId4)
                          .setCellFeature(CellFeatureDescription().setType(Enums::CellFunction::CONSTRUCTOR))});

    cluster.setPos(cluster.getClusterPosFromCells());

    DataDescription origData;
    origData.addCluster(cluster);

    IntegrationTestHelper::updateData(_access, origData);

    //perform test
    IntegrationTestHelper::runSimulation(1, _controller);

    //check results
    DataDescription newData = IntegrationTestHelper::getContent(_access, {{0, 0}, {_universeSize.x, _universeSize.y}});
    checkEnergy(origData, newData);

    auto const& newCluster = newData.clusters->at(0);

    TestResult result;
    result.movementOfCenter = *newCluster.pos - *cluster.pos;
    result.increaseNumberOfCells = newCluster.cells->size() - cluster.cells->size();

    auto newCellByCellId = IntegrationTestHelper::getCellByCellId(newData);

    auto const& newCell4 = newCellByCellId.at(cellId4);
    auto const& newToken = newCell4.tokens->at(0);

    result.token = newToken;
    result.constructorCell = newCell4;

    newCellByCellId.erase(cellId1);
    newCellByCellId.erase(cellId2);
    newCellByCellId.erase(cellId3);
    newCellByCellId.erase(cellId4);
    if (!newCellByCellId.empty()) {
        result.constructedCell = newCellByCellId.begin()->second;
    }

    return result;
}

TokenDescription ConstructorGpuTests::createTokenForConstruction(TokenForConstructionParameters tokenParameters) const
{
    auto token = createSimpleToken();
    auto& tokenData = *token.data;
    tokenData[Enums::Constr::IN] = tokenParameters._constructionInput;
    tokenData[Enums::Constr::IN_OPTION] = tokenParameters._constructionOption;
    tokenData[Enums::Constr::INOUT_ANGLE] = QuantityConverter::convertAngleToData(tokenParameters._angle);
    tokenData[Enums::Constr::IN_DIST] = QuantityConverter::convertDistanceToData(tokenParameters._distance);
    tokenData[Enums::Constr::IN_CELL_MAX_CONNECTIONS] = tokenParameters._maxConnections;
    tokenData[Enums::Constr::IN_CELL_BRANCH_NO] = tokenParameters._cellBranchNumber;
    tokenData[Enums::Constr::IN_CELL_FUNCTION_DATA] = tokenParameters._staticData.size();
    tokenData.replace(
        Enums::Constr::IN_CELL_FUNCTION_DATA + 1, tokenParameters._staticData.size(), tokenParameters._staticData);
    int const mutableDataIndex = Enums::Constr::IN_CELL_FUNCTION_DATA + 1 + tokenParameters._staticData.size();
    tokenData[mutableDataIndex] = tokenParameters._mutableData.size();
    tokenData.replace(mutableDataIndex + 1, tokenParameters._mutableData.size(), tokenParameters._mutableData);

    token.energy = tokenParameters._energy.get_value_or(
        2 * _parameters.tokenMinEnergy + 2 * _parameters.cellFunctionConstructorOffspringCellEnergy);
    return token;
}

void ConstructorGpuTests::checkResult(TestResult const& testResult, Expectations const& expectations) const
{
    auto const& token = testResult.token;
    if (Enums::ConstrIn::DO_NOTHING == token.data->at(Enums::Constr::IN)) {
        EXPECT_FALSE(testResult.constructedCell);
        return;
    }

    EXPECT_EQ(expectations._tokenOutput, token.data->at(Enums::Constr::OUT));

    if (!expectations._destruction) {
        EXPECT_TRUE(isCompatible(testResult.movementOfCenter, QVector2D{}));

        if (Enums::ConstrOut::SUCCESS == expectations._tokenOutput) {
            EXPECT_TRUE(testResult.constructedCell);
            EXPECT_TRUE(isCompatible(
                _parameters.cellFunctionConstructorOffspringCellEnergy,
                static_cast<float>(*testResult.constructedCell->energy)));

            auto const option = token.data->at(Enums::Constr::IN_OPTION);
            auto const expectedMaxConnections = token.data->at(Enums::Constr::IN_CELL_MAX_CONNECTIONS);
            auto const expectedBranchNumber = token.data->at(Enums::Constr::IN_CELL_BRANCH_NO);
            auto const expectedCellFunctionType = token.data->at(Enums::Constr::IN_CELL_FUNCTION);

            auto const expectedStaticDataLength = token.data->at(Enums::Constr::IN_CELL_FUNCTION_DATA);
            auto const expectedStaticData =
                token.data->mid(Enums::Constr::IN_CELL_FUNCTION_DATA + 1, expectedStaticDataLength);
            auto const mutableDataIndex = Enums::Constr::IN_CELL_FUNCTION_DATA + 1 + expectedStaticDataLength;
            auto const expectedMutableDataLength = token.data->at(mutableDataIndex);
            auto const expectedMutableData = token.data->mid(mutableDataIndex + 1, expectedMutableDataLength);

            EXPECT_EQ(expectedBranchNumber, *testResult.constructedCell->tokenBranchNumber);
            EXPECT_EQ(expectedCellFunctionType, testResult.constructedCell->cellFeature->type);
            EXPECT_EQ(expectedStaticData, testResult.constructedCell->cellFeature->constData);
            EXPECT_EQ(expectedMutableData, testResult.constructedCell->cellFeature->volatileData);

            auto const isSeparated = Enums::ConstrInOption::FINISH_WITH_SEP == option
                || Enums::ConstrInOption::FINISH_WITH_SEP_RED == option
                || Enums::ConstrInOption::FINISH_WITH_TOKEN_SEP_RED == option;

            {
                auto const& connectingCells = *testResult.constructedCell->connectingCells;
                EXPECT_EQ(
                    !isSeparated,
                    std::find(connectingCells.begin(), connectingCells.end(), testResult.constructorCell.id)
                        != connectingCells.end());
            }
            {
                auto const& connectingCells = *testResult.constructorCell.connectingCells;
                EXPECT_EQ(
                    !isSeparated,
                    std::find(connectingCells.begin(), connectingCells.end(), testResult.constructedCell->id)
                        != connectingCells.end());
            }
            EXPECT_PRED3(
                predEqual,
                0,
                (*testResult.constructorCell.pos + *expectations._constructedCellRelPos
                 - *testResult.constructedCell->pos)
                    .length(),
                0.05);

            auto const isFinished = Enums::ConstrInOption::FINISH_NO_SEP == option
                || Enums::ConstrInOption::FINISH_WITH_SEP == option
                || Enums::ConstrInOption::FINISH_WITH_SEP_RED == option
                || Enums::ConstrInOption::FINISH_WITH_TOKEN_SEP_RED == option;
            EXPECT_EQ(!isFinished, testResult.constructedCell->tokenBlocked);

            if (expectedMaxConnections == 0) {
                EXPECT_EQ(
                    std::max(static_cast<int>(testResult.constructedCell->connectingCells->size()), 2),
                    *testResult.constructedCell->maxConnections);
            } else {
                EXPECT_EQ(expectedMaxConnections, *testResult.constructedCell->maxConnections);
            }

            if (expectations._constructedToken) {
                auto const& actualTokens = testResult.constructedCell->tokens;
                EXPECT_EQ(1, actualTokens->size());
                EXPECT_TRUE(isCompatible(*expectations._constructedToken, actualTokens->at(0)));
            }

            return;
        } else {
            EXPECT_FALSE(testResult.constructedCell);
        }
    }
}

TEST_F(ConstructorGpuTests, testDoNothing)
{
    auto const token =
        createTokenForConstruction(TokenForConstructionParameters().constructionInput(Enums::ConstrIn::DO_NOTHING));
    auto result = runConstructionOnLineClusterTest(ConstructionOnLineClusterTestParameters().token(token));
    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::SUCCESS));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_standardParameters)
{
    auto const token =
        createTokenForConstruction(TokenForConstructionParameters().constructionInput(Enums::ConstrIn::SAFE));
    auto result = runConstructionOnLineClusterTest(ConstructionOnLineClusterTestParameters().token(token));
    auto const expectedCellPos = QVector2D{_offspringDistance, 0};
    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_nonStandardParameters1)
{
    auto const token = createTokenForConstruction(TokenForConstructionParameters()
                                                      .constructionInput(Enums::ConstrIn::SAFE)
                                                      .cellBranchNumber(2)
                                                      .maxConnections(3)
                                                      .cellFunctionType(Enums::CellFunction::SCANNER));
    auto result = runConstructionOnLineClusterTest(ConstructionOnLineClusterTestParameters().token(token));
    auto const expectedCellPos = QVector2D{_offspringDistance, 0};
    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_nonStandardParameters2)
{
    auto const basicFacade = ServiceLocator::getInstance().getService<ModelBasicBuilderFacade>();
    auto const compiler =
        basicFacade->buildCellComputerCompiler(_context->getSymbolTable(), _context->getSimulationParameters());

    std::stringstream stream;
    stream << "mov [1], 3";
    for (int i = 0; i < _parameters.cellFunctionComputerMaxInstructions - 1; ++i) {
        stream << "\nmov [1], 3";
    }

    CompilationResult compiledProgram = compiler->compileSourceCode(stream.str());
    CHECK(compiledProgram.compilationOk);

    auto const token =
        createTokenForConstruction(TokenForConstructionParameters()
                                       .constructionInput(Enums::ConstrIn::SAFE)
                                       .cellBranchNumber(1)
                                       .maxConnections(2)
                                       .cellFunctionType(Enums::CellFunction::COMPUTER)
                                       .staticData(compiledProgram.compilation)
                                       .mutableData(QByteArray(_parameters.cellFunctionComputerCellMemorySize, 1)));
    auto result = runConstructionOnLineClusterTest(ConstructionOnLineClusterTestParameters().token(token));
    auto const expectedCellPos = QVector2D{_offspringDistance, 0};
    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_ignoreDistanceOnFirstConstructedCell1)
{
    auto const token = createTokenForConstruction(
        TokenForConstructionParameters().constructionInput(Enums::ConstrIn::SAFE).distance(_offspringDistance / 2));
    auto const result = runConstructionOnLineClusterTest(ConstructionOnLineClusterTestParameters().token(token));

    auto const expectedCellPos = QVector2D{_offspringDistance, 0};
    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_ignoreDistanceOnFirstConstructedCell2)
{
    auto const token = createTokenForConstruction(
        TokenForConstructionParameters().constructionInput(Enums::ConstrIn::SAFE).distance(_offspringDistance * 2));
    auto const result = runConstructionOnLineClusterTest(ConstructionOnLineClusterTestParameters().token(token));

    auto const expectedCellPos = QVector2D{_offspringDistance, 0};
    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_rightHandSide)
{
    auto const token = createTokenForConstruction(
        TokenForConstructionParameters().constructionInput(Enums::ConstrIn::SAFE).angle(90.0f));
    auto const result = runConstructionOnLineClusterTest(ConstructionOnLineClusterTestParameters().token(token));

    auto const expectedCellPos = QVector2D{0, _offspringDistance};
    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_leftHandSide)
{
    auto const token = createTokenForConstruction(
        TokenForConstructionParameters().constructionInput(Enums::ConstrIn::SAFE).angle(-90.0f));
    auto const result = runConstructionOnLineClusterTest(ConstructionOnLineClusterTestParameters().token(token));

    auto const expectedCellPos = QVector2D{0, -_offspringDistance};
    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_errorMaxConnectionsReached)
{
    _parameters.cellMaxBonds = 1;
    _context->setSimulationParameters(_parameters);

    auto const token =
        createTokenForConstruction(TokenForConstructionParameters().constructionInput(Enums::ConstrIn::SAFE));
    auto const result = runConstructionOnLineClusterTest(ConstructionOnLineClusterTestParameters().token(token));

    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::ERROR_CONNECTION));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_errorNoEnergy)
{
    auto const lowTokenEnergy = _parameters.tokenMinEnergy + _parameters.cellFunctionConstructorOffspringCellEnergy / 2;
    auto const token = createTokenForConstruction(
        TokenForConstructionParameters().constructionInput(Enums::ConstrIn::SAFE).energy(lowTokenEnergy));
    auto const result = runConstructionOnLineClusterTest(ConstructionOnLineClusterTestParameters().token(token));

    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::ERROR_NO_ENERGY));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_otherClusterRightObstacle_safeMode)
{
    auto const token =
        createTokenForConstruction(TokenForConstructionParameters().constructionInput(Enums::ConstrIn::SAFE));
    auto const result = runConstructionOnLineClusterTest(
        ConstructionOnLineClusterTestParameters().token(token).horizontalObstacleAt(_offspringDistance));

    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::ERROR_OBSTACLE));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_otherClusterRightObstacle_unsafeMode)
{
    auto const token =
        createTokenForConstruction(TokenForConstructionParameters().constructionInput(Enums::ConstrIn::UNSAFE));
    auto const result = runConstructionOnLineClusterTest(
        ConstructionOnLineClusterTestParameters().token(token).horizontalObstacleAt(_offspringDistance));

    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::ERROR_OBSTACLE));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_otherClusterRightObstacle_brutforceMode)
{
    auto const token =
        createTokenForConstruction(TokenForConstructionParameters().constructionInput(Enums::ConstrIn::BRUTEFORCE));
    auto const result = runConstructionOnLineClusterTest(
        ConstructionOnLineClusterTestParameters().token(token).horizontalObstacleAt(_offspringDistance));

    auto const expectedCellPos = QVector2D{_offspringDistance, 0};
    checkResult(
        result,
        Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos).destruction(true));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_otherClusterLeftObstacle_safeMode)
{
    auto const token =
        createTokenForConstruction(TokenForConstructionParameters().constructionInput(Enums::ConstrIn::SAFE));
    auto const result = runConstructionOnLineClusterTest(
        ConstructionOnLineClusterTestParameters().token(token).horizontalObstacleAt(-_offspringDistance));

    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::ERROR_OBSTACLE));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_otherClusterLeftObstacle_unsafeMode)
{
    auto const token =
        createTokenForConstruction(TokenForConstructionParameters().constructionInput(Enums::ConstrIn::UNSAFE));
    auto const result = runConstructionOnLineClusterTest(
        ConstructionOnLineClusterTestParameters().token(token).horizontalObstacleAt(-_offspringDistance));

    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::ERROR_OBSTACLE));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_otherClusterLeftObstacle_brutforceMode)
{
    auto const token =
        createTokenForConstruction(TokenForConstructionParameters().constructionInput(Enums::ConstrIn::BRUTEFORCE));
    auto const result = runConstructionOnLineClusterTest(
        ConstructionOnLineClusterTestParameters().token(token).horizontalObstacleAt(-_offspringDistance));

    auto const expectedCellPos = QVector2D{_offspringDistance, 0};
    checkResult(
        result,
        Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos).destruction(true));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_sameClusterObstacle_safeMode)
{
    auto const token =
        createTokenForConstruction(TokenForConstructionParameters().constructionInput(Enums::ConstrIn::SAFE).angle(90));
    auto const result = runConstructionOnWedgeClusterTest(token, 180, 0);

    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::ERROR_OBSTACLE));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_sameClusterObstacle_unsafeMode)
{
    auto const token = createTokenForConstruction(
        TokenForConstructionParameters().constructionInput(Enums::ConstrIn::UNSAFE).angle(90));
    auto const result = runConstructionOnWedgeClusterTest(token, 180, 0);

    auto const expectedCellPos = QVector2D{0, _offspringDistance};
    checkResult(
        result,
        Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos).destruction(true));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_sameClusterObstacle_brutforceMode)
{
    auto const token = createTokenForConstruction(
        TokenForConstructionParameters().constructionInput(Enums::ConstrIn::BRUTEFORCE).angle(90));
    auto const result = runConstructionOnWedgeClusterTest(token, 180, 0);

    auto const expectedCellPos = QVector2D{0, _offspringDistance};
    checkResult(
        result,
        Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos).destruction(true));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnWedgeCluster_rightHandSide)
{
    auto const token =
        createTokenForConstruction(TokenForConstructionParameters().constructionInput(Enums::ConstrIn::SAFE));
    auto const result = runConstructionOnWedgeClusterTest(token, 90, 0);

    auto const expectedCellPos = QVector2D{_offspringDistance, 0};
    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnWedgeCluster_leftHandSide)
{
    auto const token =
        createTokenForConstruction(TokenForConstructionParameters().constructionInput(Enums::ConstrIn::SAFE));
    auto const result = runConstructionOnWedgeClusterTest(token, 270, 0);

    auto const expectedCellPos = QVector2D{-_offspringDistance, 0};
    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnWedgeCluster_diagonal)
{
    auto const token =
        createTokenForConstruction(TokenForConstructionParameters().constructionInput(Enums::ConstrIn::SAFE));
    auto const result = runConstructionOnWedgeClusterTest(token, 90, 45);

    auto const expectedCellPos = QVector2D{_offspringDistance / sqrtf(2), _offspringDistance / sqrtf(2)};
    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnTiangleCluster)
{
    auto const token =
        createTokenForConstruction(TokenForConstructionParameters().constructionInput(Enums::ConstrIn::SAFE));
    auto const result = runConstructionOnTriangleClusterTest(token);

    auto const expectedCellPos = QVector2D{_offspringDistance, 0};
    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_withEmptyToken)
{
    auto const cellBranchNumber = 1;
    auto const token = createTokenForConstruction(TokenForConstructionParameters()
                                                      .constructionInput(Enums::ConstrIn::SAFE)
                                                      .constructionOption(Enums::ConstrInOption::CREATE_EMPTY_TOKEN)
                                                      .cellBranchNumber(cellBranchNumber));
    auto const result = runConstructionOnLineClusterTest(ConstructionOnLineClusterTestParameters().token(token));
    auto const expectedCellPos = QVector2D{_offspringDistance, 0};
    QByteArray expectedTokenMemory(_parameters.tokenMemorySize, 0);
    expectedTokenMemory[0] = cellBranchNumber;
    auto const expectedToken = TokenDescription()
                                   .setEnergy(_parameters.cellFunctionConstructorOffspringTokenEnergy)
                                   .setData(expectedTokenMemory);
    checkResult(
        result,
        Expectations()
            .tokenOutput(Enums::ConstrOut::SUCCESS)
            .constructedCellRelPos(expectedCellPos)
            .constructedToken(expectedToken));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_withDuplicatedToken)
{
    auto const cellBranchNumber = 1;
    auto const token = createTokenForConstruction(TokenForConstructionParameters()
                                                      .constructionInput(Enums::ConstrIn::SAFE)
                                                      .constructionOption(Enums::ConstrInOption::CREATE_DUP_TOKEN)
                                                      .cellBranchNumber(cellBranchNumber));
    auto const result = runConstructionOnLineClusterTest(ConstructionOnLineClusterTestParameters().token(token));
    auto const expectedCellPos = QVector2D{_offspringDistance, 0};
    auto expectedTokenMemory = *token.data;
    expectedTokenMemory[0] = cellBranchNumber;
    auto const expectedToken = TokenDescription()
                                   .setEnergy(_parameters.cellFunctionConstructorOffspringTokenEnergy)
                                   .setData(expectedTokenMemory);
    checkResult(
        result,
        Expectations()
            .tokenOutput(Enums::ConstrOut::SUCCESS)
            .constructedCellRelPos(expectedCellPos)
            .constructedToken(expectedToken));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_finishWithoutSeparation)
{
    auto const token = createTokenForConstruction(TokenForConstructionParameters()
                                                      .constructionInput(Enums::ConstrIn::SAFE)
                                                      .constructionOption(Enums::ConstrInOption::FINISH_NO_SEP));
    auto const result = runConstructionOnLineClusterTest(ConstructionOnLineClusterTestParameters().token(token));
    auto const expectedCellPos = QVector2D{_offspringDistance, 0};
    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_finishSeparation)
{
    auto const token = createTokenForConstruction(TokenForConstructionParameters()
                                                      .constructionInput(Enums::ConstrIn::SAFE)
                                                      .constructionOption(Enums::ConstrInOption::FINISH_WITH_SEP));
    auto const result = runConstructionOnLineClusterTest(ConstructionOnLineClusterTestParameters().token(token));
    auto const expectedCellPos = QVector2D{_offspringDistance * 2, 0};
    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_finishSeparation_otherClusterRightObstacle_safeMode)
{
    auto const token = createTokenForConstruction(TokenForConstructionParameters()
                                                      .constructionInput(Enums::ConstrIn::SAFE)
                                                      .constructionOption(Enums::ConstrInOption::FINISH_WITH_SEP));
    auto const result = runConstructionOnLineClusterTest(
        ConstructionOnLineClusterTestParameters().token(token).horizontalObstacleAt(_offspringDistance * 2));

    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::ERROR_OBSTACLE));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_finishSeparation_otherClusterRightObstacle_unsafeMode)
{
    auto const token = createTokenForConstruction(TokenForConstructionParameters()
        .constructionInput(Enums::ConstrIn::UNSAFE)
        .constructionOption(Enums::ConstrInOption::FINISH_WITH_SEP));
    auto const result = runConstructionOnLineClusterTest(
        ConstructionOnLineClusterTestParameters().token(token).horizontalObstacleAt(_offspringDistance * 2));

    checkResult(result, Expectations().tokenOutput(Enums::ConstrOut::ERROR_OBSTACLE));
}

TEST_F(ConstructorGpuTests, testConstructFirstCellOnLineCluster_finishSeparation_otherClusterRightObstacle_bruteforceMode)
{
    auto const token = createTokenForConstruction(TokenForConstructionParameters()
        .constructionInput(Enums::ConstrIn::BRUTEFORCE)
        .constructionOption(Enums::ConstrInOption::FINISH_WITH_SEP));
    auto const result = runConstructionOnLineClusterTest(
        ConstructionOnLineClusterTestParameters().token(token).horizontalObstacleAt(_offspringDistance * 2));

    auto const expectedCellPos = QVector2D{ _offspringDistance * 2, 0 };
    checkResult(
        result,
        Expectations().tokenOutput(Enums::ConstrOut::SUCCESS).constructedCellRelPos(expectedCellPos).destruction(true));
}
