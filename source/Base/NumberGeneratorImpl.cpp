#include <sstream>
#include <thread>

#include <QRandomGenerator>

#include "NumberGeneratorImpl.h"

namespace
{
    double const RandMax = 4294967296.0;
}

NumberGeneratorImpl::NumberGeneratorImpl(QObject * parent)
	: NumberGenerator(parent)
{
}

void NumberGeneratorImpl::init(uint32_t arraySize, uint16_t threadId)
{
	_threadId = static_cast<uint64_t>(threadId) << 48;
	_arrayOfRandomNumbers.clear();
	_runningNumber = 0;
	for (uint32_t i = 0; i < arraySize; ++i) {
        _arrayOfRandomNumbers.push_back(QRandomGenerator::global()->generate());
	}
}

uint32_t NumberGeneratorImpl::getRandomInt()
{
	return getNumberFromArray();
}

uint32_t NumberGeneratorImpl::getRandomInt(uint32_t range)
{
	return getNumberFromArray() % range;
}

uint32_t NumberGeneratorImpl::getRandomInt(uint32_t min, uint32_t max)
{
    auto delta = max - min + 1;
    return min + (getNumberFromArray() % delta);
}

uint32_t NumberGeneratorImpl::getLargeRandomInt(uint32_t range)
{
	return getNumberFromArray() % (range + 1);
}

double NumberGeneratorImpl::getRandomReal(double min, double max)
{
	return static_cast<double>(getLargeRandomInt((max - min) * 1000) / 1000.0 + min);
}

double NumberGeneratorImpl::getRandomReal()
{
    return static_cast<double>(getNumberFromArray()) / RandMax;
}

QByteArray NumberGeneratorImpl::getRandomArray(int length)
{
	QByteArray bytes;
	for (int i = 0; i < length; ++i) {
		bytes[i] =getRandomInt(256);
	}

	return std::move(bytes);
}

uint64_t NumberGeneratorImpl::getId()
{
	return _threadId | ++_runningNumber;
}

uint32_t NumberGeneratorImpl::getNumberFromArray()
{
	_index = (_index + 1) % _arrayOfRandomNumbers.size();
	return _arrayOfRandomNumbers[_index];
}
