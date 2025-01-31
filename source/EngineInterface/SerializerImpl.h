#pragma once

#include <QObject>

#include "Serializer.h"
#include "Definitions.h"

class SerializerImpl
	: public Serializer
{
	Q_OBJECT

public:
	SerializerImpl(QObject *parent = nullptr);
	virtual ~SerializerImpl() = default;

    virtual void init(
        SimulationControllerBuildFunc const& controllerBuilder,
        SimulationAccessBuildFunc const& accessBuilder) override;   //only for (de)serialization of entire simulation necessary

    virtual void serialize(SimulationController* simController, int typeId, boost::optional<Settings> newSettings = boost::none) override;
    virtual SerializedSimulation const& retrieveSerializedSimulation() override;
    virtual SimulationController* deserializeSimulation(SerializedSimulation const& data) override;

	virtual string serializeDataDescription(DataDescription const& desc) const override;
	virtual DataDescription deserializeDataDescription(string const& data) override;

	virtual string serializeSymbolTable(SymbolTable const* symbolTable) const override;
	virtual SymbolTable* deserializeSymbolTable(string const& data) override;

	virtual string serializeSimulationParameters(SimulationParameters const& parameters) const override;
    virtual SimulationParameters deserializeSimulationParameters(string const& data) override;

    virtual string serializeGeneralSettings(IntVector2D const& worldSize, std::map<std::string, int> const& gpuSettings)
        const;
    virtual std::pair<IntVector2D, std::map<std::string, int>> deserializeGeneralSettings(
        std::string const& data) const;

private:
	Q_SLOT void dataReadyToRetrieve();

	void buildAccess(SimulationController* controller);

	SimulationControllerBuildFunc _controllerBuilder;
	SimulationAccessBuildFunc _accessBuilder;
	SimulationAccess* _access = nullptr;
    DescriptionHelper* _descHelper = nullptr;

	struct ConfigToSerialize {
		SimulationParameters parameters;
		SymbolTable const* symbolTable;
		IntVector2D universeSize;
		int typeId;
		map<string, int> typeSpecificData;
		int timestep;
	};
	ConfigToSerialize _configToSerialize;
    struct DuplicationSettings
    {
        bool enabled = false;
        IntVector2D origUniverseSize;
        IntVector2D count;
    };
    DuplicationSettings _duplicationSettings;
    SerializedSimulation _serializedSimulation;

	list<QMetaObject::Connection> _connections;
};
