// Prints what src/fit makes of a FIT file as "key value" lines, for comparing
// against an independent decoder (compare_fit.py uses fitparse).
//
//   g++ -std=c++17 -I src tools/fitcheck/fitdump.cpp src/fit/*.cpp src/gfdi/crc.cpp -o /tmp/fitdump
//   /tmp/fitdump ride.fit

#include "fit/activity.h"

#include <cstdio>
#include <fstream>
#include <iterator>

static void num(const char *key, double v)
{
    if (std::isnan(v))
        std::printf("%s nan\n", key);
    else
        std::printf("%s %.4f\n", key, v);
}

int main(int argc, char **argv)
{
    if (argc < 2)
        return 2;
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Fit::Activity a;
    std::string err;
    if (!Fit::parseActivity(bytes, a, err)) {
        std::printf("error %s\n", err.c_str());
        return 1;
    }
    const Fit::Summary &s = a.summary;
    std::printf("crc_ok %d\n", a.stats.crcOk ? 1 : 0);
    std::printf("truncated %d\n", a.stats.truncated ? 1 : 0);
    std::printf("start %lld\n", static_cast<long long>(s.startTime));
    std::printf("sport %d\nsub_sport %d\n", s.sport, s.subSport);
    num("elapsed", s.elapsed);
    num("timer", s.timer);
    num("distance", s.distance);
    num("avg_speed", s.avgSpeed);
    num("max_speed", s.maxSpeed);
    std::printf("avg_hr %d\nmax_hr %d\navg_cad %d\nmax_cad %d\navg_power %d\nmax_power %d\ncalories %d\n",
                s.avgHeartRate, s.maxHeartRate, s.avgCadence, s.maxCadence, s.avgPower, s.maxPower, s.calories);
    num("ascent", s.ascent);
    num("descent", s.descent);
    num("min_alt", s.minAltitude);
    num("max_alt", s.maxAltitude);
    num("avg_temp", s.avgTemperature);
    std::printf("product_name %s\n", s.productName.c_str());
    std::printf("records %zu\nlaps %zu\n", a.samples.size(), a.laps.size());
    // A digest of every record, so one wrong value anywhere shows.
    double sumLat = 0, sumLon = 0, sumAlt = 0, sumDist = 0, sumSpeed = 0;
    long sumHr = 0, sumPow = 0, sumCad = 0, withPos = 0;
    long long sumTime = 0;
    for (const Fit::Sample &x : a.samples) {
        sumTime += x.time;
        if (x.hasPosition()) {
            sumLat += x.lat;
            sumLon += x.lon;
            ++withPos;
        }
        if (!std::isnan(x.altitude)) sumAlt += x.altitude;
        if (!std::isnan(x.distance)) sumDist += x.distance;
        if (!std::isnan(x.speed)) sumSpeed += x.speed;
        if (x.heartRate >= 0) sumHr += x.heartRate;
        if (x.power >= 0) sumPow += x.power;
        if (x.cadence >= 0) sumCad += x.cadence;
    }
    std::printf("sum_time %lld\nwith_pos %ld\n", sumTime, withPos);
    num("sum_lat", sumLat);
    num("sum_lon", sumLon);
    num("sum_alt", sumAlt);
    num("sum_dist", sumDist);
    num("sum_speed", sumSpeed);
    std::printf("sum_hr %ld\nsum_power %ld\nsum_cad %ld\n", sumHr, sumPow, sumCad);
    for (size_t i = 0; i < a.laps.size(); ++i) {
        const Fit::Lap &l = a.laps[i];
        std::printf("lap%zu_start %lld\n", i, static_cast<long long>(l.startTime));
        std::printf("lap%zu_dist %.2f\n", i, std::isnan(l.distance) ? -1.0 : l.distance);
        std::printf("lap%zu_timer %.2f\n", i, std::isnan(l.timer) ? -1.0 : l.timer);
    }
    return 0;
}
