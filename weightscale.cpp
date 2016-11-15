/****************************************************************************
**
** Copyright (C) 2016 Shawn Rutledge
**
** This file is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** version 3 as published by the Free Software Foundation
** and appearing in the file LICENSE included in the packaging
** of this file.
**
** This code is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
****************************************************************************/

#include "weightscale.h"
#include <QDebug>

WeightScale::WeightScale() :
    m_discoveryAgent(nullptr), m_controller(nullptr), m_service(nullptr)
{
    m_discoveryAgent = new QBluetoothDeviceDiscoveryAgent(this);

    connect(m_discoveryAgent, SIGNAL(deviceDiscovered(const QBluetoothDeviceInfo&)),
            this, SLOT(addDevice(const QBluetoothDeviceInfo&)));
    connect(m_discoveryAgent, SIGNAL(error(QBluetoothDeviceDiscoveryAgent::Error)),
            this, SLOT(deviceScanError(QBluetoothDeviceDiscoveryAgent::Error)));
    connect(m_discoveryAgent, SIGNAL(finished()), this, SLOT(scanFinished()));
}

WeightScale::~WeightScale()
{
}

void WeightScale::deviceSearch()
{
    m_discoveryAgent->start();
    setStatus(tr("scanning for devices"));
}

void WeightScale::scanFinished()
{
    if (!m_device.isValid())
        deviceSearch();
}

void WeightScale::addDevice(const QBluetoothDeviceInfo &device)
{
    if (device.coreConfigurations() & QBluetoothDeviceInfo::LowEnergyCoreConfiguration) {
        qDebug() << "discovered device " << device.name() << device.address().toString();
        if (device.name() == QLatin1String("Electronic Scale")) {
            m_device = device;
            m_discoveryAgent->stop();
            connectService();
        }
    }
}

void WeightScale::deviceScanError(QBluetoothDeviceDiscoveryAgent::Error error)
{
    if (error == QBluetoothDeviceDiscoveryAgent::PoweredOffError)
        setStatus(tr("bluetooth adaptor is powered off"));
    else
        setStatus(tr("device scan error ") + error);
}


void WeightScale::setStatus(QString s)
{
    qDebug() << s;
    m_status = s;
    emit statusChanged();
}

QString WeightScale::status() const
{
    return m_status;
}

void WeightScale::connectService()
{
    qDebug() << m_device.name() << m_device.address();

    if (m_controller) {
        m_controller->disconnectFromDevice();
        delete m_controller;
        m_controller = nullptr;

    }
    m_controller = new QLowEnergyController(m_device, this);
    connect(m_controller, SIGNAL(serviceDiscovered(QBluetoothUuid)),
            this, SLOT(serviceDiscovered(QBluetoothUuid)));
    connect(m_controller, SIGNAL(discoveryFinished()),
            this, SLOT(serviceScanDone()));
    connect(m_controller, SIGNAL(error(QLowEnergyController::Error)),
            this, SLOT(controllerError(QLowEnergyController::Error)));
    connect(m_controller, SIGNAL(connected()),
            this, SLOT(deviceConnected()));
    connect(m_controller, SIGNAL(disconnected()),
            this, SLOT(deviceDisconnected()));

    m_controller->connectToDevice();
}

void WeightScale::deviceConnected()
{
    m_controller->discoverServices();
}

void WeightScale::deviceDisconnected()
{
    setStatus(tr("disconnected"));
    m_device = QBluetoothDeviceInfo();
    deviceSearch();
}

void WeightScale::serviceDiscovered(const QBluetoothUuid &svc)
{
    qDebug() << "discovered service" << svc << hex << svc.toUInt16();
    if (svc.toUInt16() == 0xfff0)
        m_serviceUuid = svc;
}

void WeightScale::serviceScanDone()
{
    delete m_service;
    m_service = nullptr;

    if (m_serviceUuid.isNull()) {
        setStatus(tr("no known service on ") + m_device.name());
        return;
    }

    setStatus(tr("connecting..."));
    m_service = m_controller->createServiceObject(m_serviceUuid, this);

    if (!m_service) {
        setStatus(tr("failed to connect to ") + m_device.name());
        return;
    }

    connect(m_service, SIGNAL(stateChanged(QLowEnergyService::ServiceState)),
            this, SLOT(serviceStateChanged(QLowEnergyService::ServiceState)));
    connect(m_service, SIGNAL(characteristicChanged(QLowEnergyCharacteristic,QByteArray)),
            this, SLOT(updateBodyComp(QLowEnergyCharacteristic,QByteArray)));

    m_service->discoverDetails();
}

void WeightScale::disconnectService()
{
    // disable notifications before disconnecting
    if (m_notification.isValid() && m_service
            && m_notification.value() == QByteArray::fromHex("0100")) {
        m_service->writeDescriptor(m_notification, QByteArray::fromHex("0000"));
    } else {
        m_controller->disconnectFromDevice();
        delete m_service;
        m_service = nullptr;
    }
}

void WeightScale::sendRequest()
{
    for (const QLowEnergyCharacteristic &characteristic : m_service->characteristics()) {
        qDebug() << "   characteristic " << hex << characteristic.handle() << characteristic.name() << characteristic.properties();

        switch (characteristic.properties()) {
        case QLowEnergyCharacteristic::Write:
            // TODO figure out if this is the preferences or what.  But merely subscribing for notifications
            // without writing to this characteristic seems not to be enough.
            m_service->writeCharacteristic(characteristic, QByteArray::fromHex("fe010100aa2d0285"));
            break;
        case QLowEnergyCharacteristic::Notify: {
            m_notification = characteristic.descriptor(QBluetoothUuid::ClientCharacteristicConfiguration);
            if (!m_notification.isValid()) {
                qWarning() << "invalid notification descriptor";
                return;
            }

            // enable notification
            m_service->writeDescriptor(m_notification, QByteArray::fromHex("0100"));
        }
            break;
        default:
            break;
        }
    }
}

void WeightScale::controllerError(QLowEnergyController::Error e)
{
    setStatus(tr("controller error ") + e);
}

void WeightScale::serviceStateChanged(QLowEnergyService::ServiceState s)
{
    switch (s) {
    case QLowEnergyService::ServiceDiscovered:
        sendRequest();
        break;
    default:
        break;
    }
}

void WeightScale::serviceError(QLowEnergyService::ServiceError e)
{
    setStatus(tr("service error ") + e);
}

void WeightScale::updateBodyComp(const QLowEnergyCharacteristic &c,
                                 const QByteArray &value)
{
    qDebug() << c.name() << value.toHex();
    // TODO: decode it
}
