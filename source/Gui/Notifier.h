﻿#pragma once
#include <QObject>

#include "Gui/Definitions.h"

class Notifier
	: public QObject
{
	Q_OBJECT
public:
	Notifier(QObject * parent = nullptr);
	virtual ~Notifier() = default;

	Q_SIGNAL void notifyDataRepositoryChanged(set<Receiver> const& targets, UpdateDescription update);
	Q_SIGNAL void toggleCellInfo(bool showInfo);

private:
	
};
