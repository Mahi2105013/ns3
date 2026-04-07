#include "tutorial-app.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/error-model.h"
#include "ns3/energy-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-flow-classifier.h"
#include "ns3/propagation-module.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("PaperTopologyHybrid");

/* Congestion window trace with flow id */
static void
CwndChangeWithFlow(uint32_t flowId, uint32_t oldCwnd, uint32_t newCwnd)
{
    std::cout << "Flow " << flowId << " cwnd: " << Simulator::Now().GetSeconds()
              << "\t" << newCwnd << std::endl;
}

/* Rx drop trace */
static void
RxDrop(Ptr<const Packet> p, WifiPhyRxfailureReason reason)
{
    std::cout << "RxDrop at "
              << Simulator::Now().GetSeconds()
              << " reason=" << reason
              << std::endl;
}

struct QueueSampleStats
{
    uint64_t samples = 0;
    double sumPackets = 0.0;
    uint32_t minPackets = std::numeric_limits<uint32_t>::max();
    uint32_t maxPackets = 0;
};

struct RunMeta
{
    std::string scenarioLabel;
    std::string tcpType;
    uint32_t totalNodes = 0;
    uint32_t nFlows = 0;
    double packetsPerSecond = 0.0;
    bool mobilityEnabled = false;
    double nodeSpeed = 0.0;
    double coverageSideMultiplier = 1.0;
    double coverageSideMeters = 0.0;
    double simulationTimeSeconds = 0.0;
};

static void
AppendCsvHeaderIfNeeded(const std::string& path, const std::string& header)
{
    std::ifstream in(path);
    if (!in.good())
    {
        std::ofstream out(path, std::ios::app);
        out << header << '\n';
    }
}

static void
SampleQueueOccupancy(std::vector<Ptr<WifiMacQueue>> queues,
                     std::vector<QueueSampleStats>* stats,
                     bool printTrace,
                     Time sampleInterval,
                     Time stopTime,
                     const RunMeta* meta,
                     std::ofstream* queueCsv)
{
    for (uint32_t i = 0; i < queues.size(); ++i)
    {
        uint32_t packets = 0;
        if (queues[i])
        {
            packets = queues[i]->GetNPackets();
        }

        QueueSampleStats& queueStats = stats->at(i);
        queueStats.samples += 1;
        queueStats.sumPackets += packets;
        queueStats.minPackets = std::min(queueStats.minPackets, packets);
        queueStats.maxPackets = std::max(queueStats.maxPackets, packets);

        if (printTrace)
        {
            std::cout << "Queue " << i << " packets at "
                      << Simulator::Now().GetSeconds() << " s: "
                      << packets << std::endl;
        }

        if (queueCsv && queueCsv->is_open() && meta)
        {
            (*queueCsv) << "scenario=" << meta->scenarioLabel
                        << " tcpType=" << meta->tcpType
                        << " totalNodes=" << meta->totalNodes
                        << " nFlows=" << meta->nFlows
                        << " packetsPerSecond=" << meta->packetsPerSecond
                        << " mobilityEnabled=" << (meta->mobilityEnabled ? 1 : 0)
                        << " nodeSpeed=" << meta->nodeSpeed
                        << " coverageSideMultiplier=" << meta->coverageSideMultiplier
                        << " coverageSideMeters=" << meta->coverageSideMeters
                        << " timeSec=" << Simulator::Now().GetSeconds()
                        << " queueIndex=" << i
                        << " packets=" << packets
                        << '\n';
        }
    }

    if (Simulator::Now() + sampleInterval <= stopTime)
    {
        Simulator::Schedule(sampleInterval,
                            &SampleQueueOccupancy,
                            queues,
                            stats,
                            printTrace,
                            sampleInterval,
                            stopTime,
                            meta,
                            queueCsv);
    }
}

int main(int argc, char* argv[])
{
    uint32_t nFlows = 3;
    uint32_t totalNodes = 20;
    int bottleneckBw = 2;
    std::string tcpType = "TcpCerl";
    std::string recoveryType = "TcpCerlRecovery";
    double errorRate = 0.0005;
    double bottleneckDelay = 80.0;
    double wirelessDelay = 1.0;
    uint32_t bottleneckBufferPkts = 500;
    double packetsPerSecond = 100.0;
    bool enableMobility = false;
    double nodeSpeed = 0.0;
    double queueSampleIntervalMs = 100.0;
    double coverageSideMultiplier = 1.0;
    double txRangeMeters = 10.0;
    bool printQueueTrace = false;
    bool printCwndTrace = false;
    bool printRxDropTrace = false;
    bool modifications = true;
    double cerlA = 0.55;
    double simulationTimeSeconds = 20.0;
    std::string scenarioLabel = "default";
    std::string summaryCsvPath = "results/summary.csv";
    std::string perNodeTxtPath = "results/per_node.txt";
    std::string queueTxtPath = "results/queue.txt";

    CommandLine cmd;
    cmd.AddValue("scenarioLabel", "Label for the current sweep run", scenarioLabel);
    cmd.AddValue("totalNodes", "Total nodes in the experiment (including idle nodes)", totalNodes);
    cmd.AddValue("nFlows", "Number of TCP flows", nFlows);
    cmd.AddValue("recoveryType", "Recovery algorithm", recoveryType);
    cmd.AddValue("bottleneckDelay", "Bottleneck delay (ms)", bottleneckDelay);
    cmd.AddValue("bottleneckBw", "Bottleneck bandwidth (Mbps)", bottleneckBw);
    cmd.AddValue("tcpType", "TcpNewReno or TcpCerl", tcpType);
    cmd.AddValue("errorRate", "Random packet loss rate (0.05 = 5%)", errorRate);
    cmd.AddValue("wirelessDelay", "Wireless delay (ms)", wirelessDelay);
    cmd.AddValue("bottleneckBufferPkts", "Bottleneck buffer size (packets)", bottleneckBufferPkts);
    cmd.AddValue("packetsPerSecond", "Application packet rate (packets/s)", packetsPerSecond);
    cmd.AddValue("enableMobility", "Enable mobility for sender and receiver nodes", enableMobility);
    cmd.AddValue("nodeSpeed", "Node speed when mobility is enabled (m/s)", nodeSpeed);
    cmd.AddValue("coverageSideMultiplier",
                 "Square coverage side as a multiple of Tx range (1..5)",
                 coverageSideMultiplier);
    cmd.AddValue("txRangeMeters", "Base Tx range used for square coverage (m)", txRangeMeters);
    cmd.AddValue("simulationTimeSeconds", "Simulation duration (seconds)", simulationTimeSeconds);
    cmd.AddValue("summaryCsv", "Summary CSV file path", summaryCsvPath);
    cmd.AddValue("perNodeTxt", "Per-node throughput TXT file path", perNodeTxtPath);
    cmd.AddValue("queueTxt", "Queue variation TXT file path", queueTxtPath);
    cmd.AddValue("queueSampleIntervalMs",
                 "Queue occupancy sampling interval in milliseconds",
                 queueSampleIntervalMs);
    cmd.AddValue("printQueueTrace",
                 "Print bottleneck queue occupancy at each sample",
                 printQueueTrace);
    cmd.AddValue("printCwndTrace", "Print per-flow congestion window changes", printCwndTrace);
    cmd.AddValue("printRxDropTrace", "Print PHY Rx drop trace", printRxDropTrace);
    cmd.AddValue("cerlA", "CERL A parameter", cerlA);
    cmd.AddValue("modifications", "Enable modified CERL logic", modifications);
    cmd.Parse(argc, argv);

    if (queueSampleIntervalMs <= 0.0)
    {
        std::cout << "queueSampleIntervalMs must be > 0. Using 100.0." << std::endl;
        queueSampleIntervalMs = 100.0;
    }

    if (coverageSideMultiplier < 1.0)
    {
        std::cout << "coverageSideMultiplier must be >= 1. Using 1.0." << std::endl;
        coverageSideMultiplier = 1.0;
    }
    if (coverageSideMultiplier > 5.0)
    {
        std::cout << "coverageSideMultiplier must be <= 5. Using 5.0." << std::endl;
        coverageSideMultiplier = 5.0;
    }
    if (txRangeMeters <= 0.0)
    {
        std::cout << "txRangeMeters must be > 0. Using 10.0." << std::endl;
        txRangeMeters = 10.0;
    }
    if (packetsPerSecond <= 0.0)
    {
        std::cout << "packetsPerSecond must be > 0. Using 100.0." << std::endl;
        packetsPerSecond = 100.0;
    }
    if (totalNodes < 2)
    {
        std::cout << "totalNodes must be >= 2. Using 2." << std::endl;
        totalNodes = 2;
    }
    if (nFlows < 1)
    {
        std::cout << "nFlows must be >= 1. Using 1." << std::endl;
        nFlows = 1;
    }
    if (simulationTimeSeconds <= 0.0)
    {
        std::cout << "simulationTimeSeconds must be > 0. Using 20.0." << std::endl;
        simulationTimeSeconds = 20.0;
    }

    const double coverageSideMeters = coverageSideMultiplier * txRangeMeters;
    if (totalNodes < 4)
    {
        std::cout << "totalNodes must be >= 4. Using 4." << std::endl;
        totalNodes = 4;
    }

    uint32_t usableEndNodes = totalNodes - 2;
    uint32_t nSenders = usableEndNodes / 2;
    uint32_t nReceivers = usableEndNodes - nSenders;
    if (nSenders == 0)
    {
        nSenders = 1;
        nReceivers = usableEndNodes - nSenders;
    }
    if (nReceivers == 0)
    {
        nReceivers = 1;
        nSenders = usableEndNodes - nReceivers;
    }

    RunMeta meta;
    meta.scenarioLabel = scenarioLabel;
    meta.tcpType = tcpType;
    meta.totalNodes = totalNodes;
    meta.nFlows = nFlows;
    meta.packetsPerSecond = packetsPerSecond;
    meta.mobilityEnabled = enableMobility;
    meta.nodeSpeed = nodeSpeed;
    meta.coverageSideMultiplier = coverageSideMultiplier;
    meta.coverageSideMeters = coverageSideMeters;
    meta.simulationTimeSeconds = simulationTimeSeconds;

    AppendCsvHeaderIfNeeded(summaryCsvPath,
                            "scenario,tcpType,totalNodes,nFlows,packetsPerSecond,mobilityEnabled,nodeSpeed,coverageSideMultiplier,coverageSideMeters,throughputMbps,avgDelayMs,pdr,dropRatio,energyConsumedJ");

    std::ofstream queueCsv(queueTxtPath, std::ios::app);
    if (!queueCsv.is_open())
    {
        std::cout << "Failed to open queue TXT: " << queueTxtPath << std::endl;
    }

    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       StringValue("ns3::" + tcpType));

    Config::SetDefault("ns3::TcpCerl::BottleneckBandwidth",
                       DataRateValue(DataRate(std::to_string(bottleneckBw) + "Mbps")));

    Config::SetDefault("ns3::TcpCerl::A", DoubleValue(cerlA));

    Config::SetDefault("ns3::TcpCerl::Modifications",
                       BooleanValue(modifications));

    Config::SetDefault("ns3::TcpL4Protocol::RecoveryType",
                       TypeIdValue(TypeId::LookupByName("ns3::" + recoveryType)));

    Config::SetDefault("ns3::TcpSocket::InitialCwnd",
                       UintegerValue(10));

    /* ======================
       Nodes
       ====================== */

    NodeContainer senders;
    senders.Create(nSenders);

    Ptr<Node> router1 = CreateObject<Node>();
    Ptr<Node> router2 = CreateObject<Node>();

    NodeContainer receivers;
    receivers.Create(nReceivers);

    InternetStackHelper stack;
    NodeContainer allNodes;
    allNodes.Add(senders);
    allNodes.Add(router1);
    allNodes.Add(router2);
    allNodes.Add(receivers);
    stack.Install(allNodes);

    /* ======================
       Mobility
       ====================== */

    MobilityHelper mobility;
    if (enableMobility)
    {
        mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    }
    else
    {
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    }
    mobility.InstallAll();

    const double centerY = 0.5 * coverageSideMeters;
    const double leftX = 0.10 * coverageSideMeters;
    const double router1X = 0.30 * coverageSideMeters;
    const double router2X = 0.60 * coverageSideMeters;
    const double rightX = 0.90 * coverageSideMeters;

    router1->GetObject<MobilityModel>()->SetPosition(Vector(router1X, centerY, 0.0));
    router2->GetObject<MobilityModel>()->SetPosition(Vector(router2X, centerY, 0.0));

    const double senderStepY = coverageSideMeters / static_cast<double>(nSenders + 1);
    const double receiverStepY = coverageSideMeters / static_cast<double>(nReceivers + 1);

    for (uint32_t i = 0; i < nSenders; ++i)
    {
        const double y = senderStepY * static_cast<double>(i + 1);
        senders.Get(i)->GetObject<MobilityModel>()->SetPosition(
            Vector(leftX, y, 0.0));

        if (enableMobility)
        {
            Ptr<ConstantVelocityMobilityModel> senderMobility =
                senders.Get(i)->GetObject<ConstantVelocityMobilityModel>();
            if (senderMobility)
            {
                senderMobility->SetVelocity(Vector(nodeSpeed, 0.0, 0.0));
            }
        }
    }

    for (uint32_t i = 0; i < nReceivers; ++i)
    {
        const double y = receiverStepY * static_cast<double>(i + 1);
        receivers.Get(i)->GetObject<MobilityModel>()->SetPosition(
            Vector(rightX, y, 0.0));

        if (enableMobility)
        {
            Ptr<ConstantVelocityMobilityModel> receiverMobility =
                receivers.Get(i)->GetObject<ConstantVelocityMobilityModel>();
            if (receiverMobility)
            {
                receiverMobility->SetVelocity(Vector(-nodeSpeed, 0.0, 0.0));
            }
        }
    }

    /* ======================
       WiFi Setup
       ====================== */

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211g);

    const double hopDistanceMeters = 10.0;
    auto delayMsToSpeed = [hopDistanceMeters](double delayMs) {
        const double safeDelayMs = std::max(delayMs, 0.001);
        const double delaySeconds = safeDelayMs / 1000.0;
        return hopDistanceMeters / delaySeconds;
    };

    YansWifiPhyHelper phy1;
    WifiMacHelper mac;

    const auto addSquareCoverageModel = [coverageSideMeters](YansWifiChannelHelper& channel) {
        channel.AddPropagationLoss("ns3::RangePropagationLossModel",
                                   "MaxRange",
                                   DoubleValue(coverageSideMeters));
    };

    /* ======================
         Wireless rate setup
       ====================== */

    std::string wifiRate;

    if (bottleneckBw <= 2)
    {
        wifiRate = "DsssRate2Mbps";
    }
    else if (bottleneckBw <= 6)
    {
        wifiRate = "ErpOfdmRate6Mbps";
    }
    else if (bottleneckBw <= 12)
    {
        wifiRate = "ErpOfdmRate12Mbps";
    }
    else if (bottleneckBw <= 24)
    {
        wifiRate = "ErpOfdmRate24Mbps";
    }
    else
    {
        wifiRate = "ErpOfdmRate54Mbps";
    }

    wifi.SetRemoteStationManager(
        "ns3::ConstantRateWifiManager",
        "DataMode", StringValue(wifiRate),
        "ControlMode", StringValue(wifiRate));

    /* ======================
       Wireless links
       ====================== */

    mac.SetType("ns3::AdhocWifiMac");

    PointToPointHelper p2pSender;
    p2pSender.SetDeviceAttribute("DataRate", StringValue("100Mbps"));
    p2pSender.SetChannelAttribute("Delay", StringValue("1ms"));

    std::vector<NetDeviceContainer> senderLinkDevices(nSenders);
    std::vector<NetDeviceContainer> receiverLinkDevices(nReceivers);

    for (uint32_t i = 0; i < nSenders; ++i)
    {
        NodeContainer leftPair;
        leftPair.Add(senders.Get(i));
        leftPair.Add(router1);
        senderLinkDevices[i] = p2pSender.Install(leftPair);
    }

    PointToPointHelper p2pBottleneck;
    p2pBottleneck.SetDeviceAttribute("DataRate", StringValue(std::to_string(bottleneckBw) + "Mbps"));
    p2pBottleneck.SetChannelAttribute("Delay", StringValue(std::to_string(static_cast<int>(bottleneckDelay)) + "ms"));

    NodeContainer bottleneckPair;
    bottleneckPair.Add(router1);
    bottleneckPair.Add(router2);
    NetDeviceContainer bottleneckDevices = p2pBottleneck.Install(bottleneckPair);

    for (uint32_t i = 0; i < nReceivers; ++i)
    {
        YansWifiPhyHelper accessPhy;
        YansWifiChannelHelper accessChannel = YansWifiChannelHelper::Default();
        addSquareCoverageModel(accessChannel);
        accessChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel",
                                          "Speed",
                                          DoubleValue(delayMsToSpeed(wirelessDelay)));
        accessPhy.SetChannel(accessChannel.Create());

        NodeContainer rightPair;
        rightPair.Add(router2);
        rightPair.Add(receivers.Get(i));
        receiverLinkDevices[i] = wifi.Install(accessPhy, mac, rightPair);
    }

    /* ======================
       Trace PHY drops (SAFE)
       ====================== */

    auto applyRandomLoss = [errorRate](const NetDeviceContainer& devices) {
        for (uint32_t i = 0; i < devices.GetN(); ++i)
        {
            Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice>(devices.Get(i));
            if (!wifiDevice)
            {
                continue;
            }

            Ptr<RateErrorModel> em = CreateObject<RateErrorModel>();
            em->SetUnit(RateErrorModel::ERROR_UNIT_PACKET);
            em->SetRate(errorRate);

            wifiDevice->GetPhy()->SetPostReceptionErrorModel(em);
        }
    };

    for (uint32_t i = 0; i < nSenders; ++i)
    {
        applyRandomLoss(senderLinkDevices[i]);
    }
    applyRandomLoss(bottleneckDevices);
    for (uint32_t i = 0; i < nReceivers; ++i)
    {
        applyRandomLoss(receiverLinkDevices[i]);
    }

    auto setBufferSize = [bottleneckBufferPkts](const NetDeviceContainer& devices) {
        for (uint32_t i = 0; i < devices.GetN(); ++i)
        {
            Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice>(devices.Get(i));
            if (!wifiDevice)
            {
                continue;
            }

            Ptr<Txop> txop = wifiDevice->GetMac()->GetTxop();
            if (!txop)
            {
                continue;
            }

            txop->GetWifiMacQueue()->SetMaxSize(QueueSize(std::to_string(bottleneckBufferPkts) + "p"));
        }
    };

    setBufferSize(bottleneckDevices);

    NetDeviceContainer allWifiDevices;
    for (uint32_t i = 0; i < nReceivers; ++i)
    {
        allWifiDevices.Add(receiverLinkDevices[i]);
    }

    BasicEnergySourceHelper basicSourceHelper;
    basicSourceHelper.Set("BasicEnergySourceInitialEnergyJ", DoubleValue(1000.0));
    WifiRadioEnergyModelHelper radioEnergyHelper;
    radioEnergyHelper.Set("TxCurrentA", DoubleValue(0.0174));

    energy::DeviceEnergyModelContainer energyModels;
    for (uint32_t i = 0; i < allWifiDevices.GetN(); ++i)
    {
        Ptr<NetDevice> dev = allWifiDevices.Get(i);
        if (!dev)
        {
            continue;
        }

        Ptr<Node> devNode = dev->GetNode();
        if (!devNode)
        {
            continue;
        }

        energy::EnergySourceContainer sourceContainer = basicSourceHelper.Install(devNode);
        if (sourceContainer.GetN() == 0)
        {
            continue;
        }

        energyModels.Add(radioEnergyHelper.Install(dev, sourceContainer.Get(0)));
    }

    std::vector<Ptr<WifiMacQueue>> bottleneckQueues;
    std::vector<QueueSampleStats> bottleneckQueueStats;
    for (uint32_t i = 0; i < bottleneckDevices.GetN(); ++i)
    {
        Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice>(bottleneckDevices.Get(i));
        if (!wifiDevice)
        {
            continue;
        }

        Ptr<Txop> txop = wifiDevice->GetMac()->GetTxop();
        if (!txop)
        {
            continue;
        }

        bottleneckQueues.push_back(txop->GetWifiMacQueue());
        bottleneckQueueStats.push_back(QueueSampleStats{});
    }

    if (!bottleneckQueues.empty())
    {
        Simulator::Schedule(Seconds(0.0),
                            &SampleQueueOccupancy,
                            bottleneckQueues,
                            &bottleneckQueueStats,
                            printQueueTrace,
                            MilliSeconds(queueSampleIntervalMs),
                            Seconds(simulationTimeSeconds),
                            &meta,
                            &queueCsv);
    }

    std::cout << "Configured delays: wireless=" << wirelessDelay
              << " ms, bottleneck=" << bottleneckDelay << " ms" << std::endl;
    std::cout << "Square coverage side: " << coverageSideMeters
              << " m (" << coverageSideMultiplier << " x Tx range)" << std::endl;
    std::cout << "Total nodes: " << totalNodes << " (senders=" << nSenders
              << ", receivers=" << nReceivers << ", routers=2)" << std::endl;
    std::cout << "Packets per second: " << packetsPerSecond << std::endl;
    std::cout << "Mobility enabled: " << (enableMobility ? "true" : "false")
              << ", node speed=" << nodeSpeed << " m/s" << std::endl;
    std::cout << "Configured bottleneck buffer: " << bottleneckBufferPkts
              << " packets" << std::endl;
    std::cout << "Queue sample interval (ms): " << queueSampleIntervalMs << std::endl;
    std::cout << "Print queue trace: " << (printQueueTrace ? "true" : "false") << std::endl;

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll();

    Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(receiverLinkDevices[0].Get(1));
    if (wifiDev && printRxDropTrace)
    {
        Ptr<WifiPhy> phyLayer = wifiDev->GetPhy();

        phyLayer->TraceConnectWithoutContext(
            "PhyRxDrop",
            MakeCallback(&RxDrop));
    }

    /* ======================
       IP Addressing
       ====================== */

    Ipv4AddressHelper address;
    std::vector<Ipv4InterfaceContainer> senderLinkIfs(nSenders);
    std::vector<Ipv4InterfaceContainer> receiverLinkIfs(nReceivers);

    for (uint32_t i = 0; i < nSenders; ++i)
    {
        std::string subnet = "10.1." + std::to_string(i + 1) + ".0";
        address.SetBase(subnet.c_str(), "255.255.255.0");
        senderLinkIfs[i] = address.Assign(senderLinkDevices[i]);
    }

    address.SetBase("10.250.1.0", "255.255.255.0");
    Ipv4InterfaceContainer bottleneckIfs = address.Assign(bottleneckDevices);

    for (uint32_t i = 0; i < nReceivers; ++i)
    {
        std::string subnet = "10.2." + std::to_string(i + 1) + ".0";
        address.SetBase(subnet.c_str(), "255.255.255.0");
        receiverLinkIfs[i] = address.Assign(receiverLinkDevices[i]);
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    /* ======================
       Sink
       ====================== */

    std::vector<uint16_t> sinkPorts;
    std::vector<uint32_t> flowSenderIndex(nFlows, 0);
    std::vector<uint32_t> flowReceiverIndex(nFlows, 0);
    ApplicationContainer sinkApps;
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        const uint32_t receiverIdx = i % nReceivers;
        flowReceiverIndex[i] = receiverIdx;

        uint16_t sinkPort = static_cast<uint16_t>(8080 + i);
        sinkPorts.push_back(sinkPort);

        PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                    InetSocketAddress(Ipv4Address::GetAny(), sinkPort));

        ApplicationContainer sinkApp = sinkHelper.Install(receivers.Get(receiverIdx));
        sinkApp.Start(Seconds(0.0));
        sinkApp.Stop(Seconds(simulationTimeSeconds));
        sinkApps.Add(sinkApp);
    }

    /* ======================
       TCP Flow
       ====================== */

    std::vector<Ptr<TutorialApp>> apps;
    apps.reserve(nFlows);
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        const uint32_t senderIdx = i % nSenders;
        const uint32_t receiverIdx = i % nReceivers;
        flowSenderIndex[i] = senderIdx;
        flowReceiverIndex[i] = receiverIdx;

        Ptr<Socket> tcpSocket =
            Socket::CreateSocket(senders.Get(senderIdx), TcpSocketFactory::GetTypeId());

        if (printCwndTrace)
        {
            tcpSocket->TraceConnectWithoutContext(
                "CongestionWindow",
                MakeBoundCallback(&CwndChangeWithFlow, i + 1));
        }

        Address sinkAddress(InetSocketAddress(receiverLinkIfs[receiverIdx].GetAddress(1), sinkPorts.at(i)));

        Ptr<TutorialApp> app = CreateObject<TutorialApp>();
        const uint32_t packetsToSend = static_cast<uint32_t>(packetsPerSecond * simulationTimeSeconds);
        const uint32_t packetSizeBytes = 1000;
        const DataRate appRate(std::to_string(static_cast<uint64_t>(packetsPerSecond * packetSizeBytes * 8.0)) + "bps");
        app->Setup(tcpSocket,
                   sinkAddress,
               packetSizeBytes,
               packetsToSend,
               appRate);

        senders.Get(senderIdx)->AddApplication(app);
        app->SetStartTime(Seconds(1.0 + 0.01 * i));
        app->SetStopTime(Seconds(simulationTimeSeconds));
        apps.push_back(app);
    }

    Simulator::Stop(Seconds(simulationTimeSeconds));
    Simulator::Run();

    flowMonitor->CheckForLostPackets();

    double totalBytes = 0.0;
    std::vector<double> sinkThroughputMbps(nFlows, 0.0);
    std::unordered_map<uint32_t, double> senderNodeTxThroughputMbps;
    std::unordered_map<uint32_t, double> receiverNodeRxThroughputMbps;
    for (uint32_t i = 0; i < nSenders; ++i)
    {
        senderNodeTxThroughputMbps[senders.Get(i)->GetId()] = 0.0;
    }
    for (uint32_t i = 0; i < nReceivers; ++i)
    {
        receiverNodeRxThroughputMbps[receivers.Get(i)->GetId()] = 0.0;
    }

    std::unordered_map<uint16_t, uint32_t> sinkPortToFlow;
    sinkPortToFlow.reserve(nFlows);
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        sinkPortToFlow.emplace(sinkPorts[i], i);
    }

    std::vector<double> flowDelayMs(nFlows, 0.0);
    std::vector<double> flowJitterMs(nFlows, 0.0);
    std::vector<double> flowLossRatio(nFlows, 0.0);
    std::vector<double> flowDeliveryRatio(nFlows, 0.0);
    std::vector<double> flowMonitorThroughputMbps(nFlows, 0.0);

    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowHelper.GetClassifier());
    if (classifier)
    {
        const auto flowStats = flowMonitor->GetFlowStats();
        for (const auto& flow : flowStats)
        {
            Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flow.first);
            if (tuple.protocol != 6)
            {
                continue;
            }

            auto flowIt = sinkPortToFlow.find(tuple.destinationPort);
            if (flowIt == sinkPortToFlow.end())
            {
                continue;
            }

            const uint32_t idx = flowIt->second;
            const FlowMonitor::FlowStats& stat = flow.second;
            flowMonitorThroughputMbps[idx] +=
                static_cast<double>(stat.rxBytes) * 8.0 / 1e6 / simulationTimeSeconds;
            const uint32_t senderNodeId = senders.Get(flowSenderIndex[idx])->GetId();
            const uint32_t receiverNodeId = receivers.Get(flowReceiverIndex[idx])->GetId();
            senderNodeTxThroughputMbps[senderNodeId] +=
                static_cast<double>(stat.txBytes) * 8.0 / 1e6 / simulationTimeSeconds;
            receiverNodeRxThroughputMbps[receiverNodeId] +=
                static_cast<double>(stat.rxBytes) * 8.0 / 1e6 / simulationTimeSeconds;

            if (stat.rxPackets > 0)
            {
                flowDelayMs[idx] = stat.delaySum.GetSeconds() * 1000.0 /
                                   static_cast<double>(stat.rxPackets);
                flowJitterMs[idx] = stat.jitterSum.GetSeconds() * 1000.0 /
                                    static_cast<double>(stat.rxPackets);
            }
            if (stat.txPackets > 0)
            {
                flowLossRatio[idx] = static_cast<double>(stat.lostPackets) /
                                     static_cast<double>(stat.txPackets);
                flowDeliveryRatio[idx] = static_cast<double>(stat.rxPackets) /
                                        static_cast<double>(stat.txPackets);
            }
        }
    }

    for (uint32_t i = 0; i < nFlows; ++i)
    {
        Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(i));
        if (!sink)
        {
            continue;
        }

        double flowBytes = sink->GetTotalRx();
        totalBytes += flowBytes;
        sinkThroughputMbps[i] = flowBytes * 8.0 / 1e6 / simulationTimeSeconds;
        std::cout << "Flow " << (i + 1) << " throughput (Mbps): "
                  << sinkThroughputMbps[i] << std::endl;
    }
    double throughputMbps = totalBytes * 8.0 / 1e6 / simulationTimeSeconds;

    double avgDelayMs = 0.0;
    double avgDeliveryRatio = 0.0;
    double avgDropRatio = 0.0;
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        avgDelayMs += flowDelayMs[i];
        avgDeliveryRatio += flowDeliveryRatio[i];
        avgDropRatio += flowLossRatio[i];
    }
    avgDelayMs /= static_cast<double>(nFlows);
    avgDeliveryRatio /= static_cast<double>(nFlows);
    avgDropRatio /= static_cast<double>(nFlows);

    double energyConsumedJ = 0.0;
    for (auto it = energyModels.Begin(); it != energyModels.End(); ++it)
    {
        if (*it)
        {
            energyConsumedJ += (*it)->GetTotalEnergyConsumption();
        }
    }

    std::ofstream summaryCsv(summaryCsvPath, std::ios::app);
    if (summaryCsv.is_open())
    {
        summaryCsv << scenarioLabel << ','
                   << tcpType << ','
                   << totalNodes << ','
                   << nFlows << ','
                   << packetsPerSecond << ','
                   << (enableMobility ? 1 : 0) << ','
                   << nodeSpeed << ','
                   << coverageSideMultiplier << ','
                   << coverageSideMeters << ','
                   << throughputMbps << ','
                   << avgDelayMs << ','
                   << avgDeliveryRatio << ','
                   << avgDropRatio << ','
                   << energyConsumedJ << '\n';
    }

    std::ofstream perNodeTxt(perNodeTxtPath, std::ios::app);
    if (perNodeTxt.is_open())
    {
        perNodeTxt << "=== scenario=" << scenarioLabel
                   << " tcpType=" << tcpType
                   << " totalNodes=" << totalNodes
                   << " nFlows=" << nFlows
                   << " packetsPerSecond=" << packetsPerSecond
                   << " mobilityEnabled=" << (enableMobility ? 1 : 0)
                   << " nodeSpeed=" << nodeSpeed
                   << " coverageSideMultiplier=" << coverageSideMultiplier
                   << " coverageSideMeters=" << coverageSideMeters
                   << " ===" << '\n';

        for (uint32_t i = 0; i < nSenders; ++i)
        {
            const uint32_t nodeId = senders.Get(i)->GetId();
            perNodeTxt << "nodeId=" << nodeId
                       << " role=sender"
                       << " txThroughputMbps=" << senderNodeTxThroughputMbps[nodeId]
                       << " rxThroughputMbps=0" << '\n';
        }

        for (uint32_t i = 0; i < nReceivers; ++i)
        {
            const uint32_t nodeId = receivers.Get(i)->GetId();
            perNodeTxt << "nodeId=" << nodeId
                       << " role=receiver"
                       << " txThroughputMbps=0"
                       << " rxThroughputMbps=" << receiverNodeRxThroughputMbps[nodeId]
                       << '\n';
        }

        perNodeTxt << "nodeId=" << router1->GetId()
                   << " role=router txThroughputMbps=0 rxThroughputMbps=0" << '\n';
        perNodeTxt << "nodeId=" << router2->GetId()
                   << " role=router txThroughputMbps=0 rxThroughputMbps=0" << '\n';
        perNodeTxt << '\n';
    }

    std::cout << "--- Per-flow QoS metrics ---" << std::endl;
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        std::cout << "Flow " << (i + 1)
                  << " fmThroughput(Mbps)=" << flowMonitorThroughputMbps[i]
                  << " avgDelay(ms)=" << flowDelayMs[i]
                  << " avgJitter(ms)=" << flowJitterMs[i]
                  << " deliveryRatio=" << flowDeliveryRatio[i]
                  << " lossRatio=" << flowLossRatio[i]
                  << std::endl;
    }

    std::cout << "--- Per-node throughput summary (Mbps) ---" << std::endl;
    for (uint32_t i = 0; i < nSenders; ++i)
    {
        const uint32_t nodeId = senders.Get(i)->GetId();
        std::cout << "SenderNodeId=" << nodeId
                  << " txThroughput=" << senderNodeTxThroughputMbps[nodeId]
                  << std::endl;
    }
    for (uint32_t i = 0; i < nReceivers; ++i)
    {
        const uint32_t nodeId = receivers.Get(i)->GetId();
        std::cout << "ReceiverNodeId=" << nodeId
                  << " rxThroughput=" << receiverNodeRxThroughputMbps[nodeId]
                  << std::endl;
    }

    if (!bottleneckQueueStats.empty())
    {
        std::cout << "--- Bottleneck queue variation (packets) ---" << std::endl;
        if (queueCsv.is_open())
        {
            queueCsv << "--- summary scenario=" << scenarioLabel
                     << " tcpType=" << tcpType
                     << " ---" << '\n';
        }
        for (uint32_t i = 0; i < bottleneckQueueStats.size(); ++i)
        {
            const QueueSampleStats& stats = bottleneckQueueStats[i];
            const double avgPackets = (stats.samples > 0)
                                          ? stats.sumPackets / static_cast<double>(stats.samples)
                                          : 0.0;
            const uint32_t minPackets = (stats.samples > 0)
                                            ? stats.minPackets
                                            : 0;

            std::cout << "Queue " << i
                      << " avg=" << avgPackets
                      << " min=" << minPackets
                      << " max=" << stats.maxPackets
                      << " samples=" << stats.samples
                      << std::endl;

            if (queueCsv.is_open())
            {
                queueCsv << "queueIndex=" << i
                         << " avgPackets=" << avgPackets
                         << " minPackets=" << minPackets
                         << " maxPackets=" << stats.maxPackets
                         << " samples=" << stats.samples
                         << '\n';
            }
        }
        if (queueCsv.is_open())
        {
            queueCsv << '\n';
        }
    }

    std::cout << "Simulation complete." << std::endl;
    std::cout << "Average throughput (Mbps): "
              << throughputMbps << std::endl;
    std::cout << "Average end-to-end delay (ms): " << avgDelayMs << std::endl;
    std::cout << "Average packet delivery ratio: " << avgDeliveryRatio << std::endl;
    std::cout << "Average packet drop ratio: " << avgDropRatio << std::endl;
    std::cout << "Total energy consumed (J): " << energyConsumedJ << std::endl;

    Simulator::Destroy();

    return 0;
}