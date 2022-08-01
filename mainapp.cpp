#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/asio/property.hpp>

#include <cstdlib>
#include <fstream>
#include <iostream>

std::string hwmonPath = "/sys/bus/iio/devices/iio:device0/in_voltage";

namespace iio_hwmon
{

boost::asio::io_service io;
std::shared_ptr<sdbusplus::asio::connection> conn;

struct iioEntity
{
    std::shared_ptr<sdbusplus::asio::dbus_interface> valueIface;
    std::shared_ptr<sdbusplus::asio::dbus_interface> critIface;
    std::string path;
    const double critMax;
    const double critMin;
    const double r1;
    const double r2;
};
std::vector<iioEntity> entities;

static double formula(std::vector<iioEntity>::const_iterator iter,
                      int hwmonread)
{
    double value;
    double denominator = iter->r2 == 0 ? 1 : iter->r2;
    value = (hwmonread + 1) * (iter->r1 + iter->r2) /
            ((1024.0 / 1.8) * denominator);
    return value;
}

static int readIIOHwmonValue(const std::string& path, int& value)
{
    std::ifstream file(path);
    if (!file.is_open())
    {
        std::cerr << "Failed to read iio-hwmon file\n";
        return -1;
    }

    char buf[16];
    file >> buf;
    value = std::atoi(buf);
    file.close();

    return 0;
}

static void asyncReadValue(const boost::system::error_code&,
                           boost::asio::deadline_timer* timer,
                           const std::vector<iioEntity>& entities)
{
    auto iter = entities.cbegin();
    int hwmonread, id = 0;
    double value;
    while (iter != entities.cend())
    {
        if (readIIOHwmonValue(hwmonPath + std::to_string(id) + "_raw",
                              hwmonread) == -1)
        {
            std::cerr << "Failed to read value for id " << id << '\n';
            return;
        }
        if (hwmonread != 0)
        {
            value = formula(iter, hwmonread);
        }

        // set dbus value
        sdbusplus::asio::setProperty<double>(
            *conn, "xyz.openbmc_project.Hwmon.IIO", iter->path,
            "xyz.openbmc_project.Sensor.Value", "Value",
            static_cast<double>(value),
            [](const boost::system::error_code&) {});

        ++id;
        ++iter;
    }
    timer->expires_at(timer->expires_at() + boost::posix_time::seconds(2));
    timer->async_wait(boost::bind(
        asyncReadValue, boost::asio::placeholders::error, timer, entities));
}

static void createDbusObj(sdbusplus::asio::object_server& server,
                          std::vector<iioEntity>& entities)
{
    auto iter = entities.begin();
    while (iter != entities.end())
    {
        iter->path = "/xyz/openbmc_project/sensors/voltage/" + iter->path;
        iter->valueIface = server.add_interface(
            iter->path, "xyz.openbmc_project.Sensor.Value");
        iter->valueIface->register_property(
            "Value", 88.88, sdbusplus::asio::PropertyPermission::readWrite);
        iter->valueIface->initialize();
        iter->critIface = server.add_interface(
            iter->path, "xyz.openbmc_project.Sensor.Threshold.Critical");
        if (iter->path == "/xyz/openbmc_project/sensors/voltage/VBAT")
        {
            iter->critIface->register_property(
                "WarningLow", 2.6,
                sdbusplus::asio::PropertyPermission::readWrite);
        }
        else
        {
            iter->critIface->register_property(
                "CriticalHigh", iter->critMax,
                sdbusplus::asio::PropertyPermission::readWrite);
        }
        iter->critIface->register_property(
            "CriticalLow", iter->critMin,
            sdbusplus::asio::PropertyPermission::readWrite);
        iter->critIface->initialize();
        ++iter;
    }
}

} // namespace iio_hwmon

int main()
{
    using namespace iio_hwmon;

    conn = std::make_shared<sdbusplus::asio::connection>(io);

    conn->request_name("xyz.openbmc_project.Hwmon.IIO");

    // init iio entities vector
    entities.push_back(
        iioEntity{nullptr, nullptr, "PLUS12V", 12.9, 11.16, 8.2, 1.0});
    entities.push_back(
        iioEntity{nullptr, nullptr, "PLUS5V", 5.37, 4.65, 3.0, 1.0});
    entities.push_back(
        iioEntity{nullptr, nullptr, "PLUS3DOT3V", 3.54, 3.06, 1.8, 1.0});
    entities.push_back(
        iioEntity{nullptr, nullptr, "PVCCIN_CPU0", 2.04, 1.56, 1.0, 3.0});
    entities.push_back(
        iioEntity{nullptr, nullptr, "PVCCIN_CPU1", 2.04, 1.56, 1.0, 3.0});
    entities.push_back(
        iioEntity{nullptr, nullptr, "PVCCIO_CPU0", 1.25, 0.75, 1.0, 1.0});
    entities.push_back(
        iioEntity{nullptr, nullptr, "PVCCIO_CPU1", 1.25, 0.75, 1.0, 1.0});
    entities.push_back(
        iioEntity{nullptr, nullptr, "VBAT", 0, 2.5, 787.0, 402.0});
    entities.push_back(
        iioEntity{nullptr, nullptr, "PVDDQ_ABCD_CPU0", 1.29, 1.11, 1.0, 0});
    entities.push_back(
        iioEntity{nullptr, nullptr, "PVDDQ_EFGH_CPU0", 1.29, 1.11, 1.0, 0});
    entities.push_back(
        iioEntity{nullptr, nullptr, "PVDDQ_ABCD_CPU1", 1.29, 1.11, 1.0, 0});
    entities.push_back(
        iioEntity{nullptr, nullptr, "PVDDQ_EFGH_CPU1", 1.29, 1.11, 1.0, 0});
    entities.push_back(
        iioEntity{nullptr, nullptr, "P1V05_PCH", 1.11, 0.99, 1.0, 0});
    entities.push_back(
        iioEntity{nullptr, nullptr, "PVNN_PCH", 1.07, 0.93, 1.0, 0});
    entities.push_back(
        iioEntity{nullptr, nullptr, "P1V8_PCH", 1.94, 1.66, 5.6, 15.0});
    entities.push_back(
        iioEntity{nullptr, nullptr, "PGPPA_PCH", 3.54, 3.06, 1.8, 1});

    // init dbus
    auto iioServer = sdbusplus::asio::object_server(conn);
    createDbusObj(iioServer, entities);

    boost::asio::deadline_timer timer(io, boost::posix_time::seconds(2));
    timer.async_wait(boost::bind(
        asyncReadValue, boost::asio::placeholders::error, &timer, entities));

    io.run();
    return 0;
}
