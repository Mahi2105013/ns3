#include "tutorial-app.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/error-model.h"

#include <algorithm>
#include <iostream>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("PaperTopologyWireless");

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

int main(int argc, char* argv[])
{
    const uint32_t nFlows = 3;
    int bottleneckBw = 2;
    std::string tcpType = "TcpCerl";
    std::string recoveryType = "TcpCerlRecovery";
    double errorRate = 0.0005;
    double bottleneckDelay = 80.0;
    double wirelessDelay = 1.0;
    uint32_t bottleneckBufferPkts = 500;

    CommandLine cmd;
    cmd.AddValue("recoveryType", "Recovery algorithm", recoveryType);
    cmd.AddValue("bottleneckDelay", "Bottleneck delay (ms)", bottleneckDelay);
    cmd.AddValue("bottleneckBw", "Bottleneck bandwidth (Mbps)", bottleneckBw);
    cmd.AddValue("tcpType", "TcpNewReno or TcpCerl", tcpType);
    cmd.AddValue("errorRate", "Random packet loss rate (0.05 = 5%)", errorRate);
    cmd.AddValue("wirelessDelay", "Wireless delay (ms)", wirelessDelay);
    cmd.AddValue("bottleneckBufferPkts", "Bottleneck buffer size (packets)", bottleneckBufferPkts);
    cmd.Parse(argc, argv);

    Config::SetDefault("ns3::TcpL4Protocol::SocketType",
                       StringValue("ns3::" + tcpType));

    Config::SetDefault("ns3::TcpCerl::BottleneckBandwidth",
                       DataRateValue(DataRate(std::to_string(bottleneckBw) + "Mbps")));

    Config::SetDefault("ns3::TcpL4Protocol::RecoveryType",
                       TypeIdValue(TypeId::LookupByName("ns3::" + recoveryType)));

    Config::SetDefault("ns3::TcpSocket::InitialCwnd",
                       UintegerValue(10));

    /* ======================
       Nodes
       ====================== */

    NodeContainer senders;
    senders.Create(nFlows);

    Ptr<Node> router1 = CreateObject<Node>();
    Ptr<Node> router2 = CreateObject<Node>();

    NodeContainer receivers;
    receivers.Create(nFlows);

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
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.InstallAll();

    router1->GetObject<MobilityModel>()->SetPosition(Vector(10.0, 0.0, 0.0));
    router2->GetObject<MobilityModel>()->SetPosition(Vector(20.0, 0.0, 0.0));

    for (uint32_t i = 0; i < nFlows; ++i)
    {
        const double y = static_cast<double>(i * 6);
        senders.Get(i)->GetObject<MobilityModel>()->SetPosition(Vector(0.0, y, 0.0));
        receivers.Get(i)->GetObject<MobilityModel>()->SetPosition(Vector(30.0, y, 0.0));
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

    /* ======================
         Wireless rate setup
       ====================== */

    std::string wifiRate;

    if (bottleneckBw == 2)
        wifiRate = "DsssRate2Mbps";
    else if (bottleneckBw == 8)
        wifiRate = "ErpOfdmRate6Mbps";
    else
        wifiRate = "ErpOfdmRate6Mbps";

    wifi.SetRemoteStationManager(
        "ns3::ConstantRateWifiManager",
        "DataMode", StringValue(wifiRate),
        "ControlMode", StringValue(wifiRate));

    /* ======================
       Wireless links
       ====================== */

    mac.SetType("ns3::AdhocWifiMac");

    std::vector<NetDeviceContainer> senderLinkDevices(nFlows);
    std::vector<NetDeviceContainer> receiverLinkDevices(nFlows);

    for (uint32_t i = 0; i < nFlows; ++i)
    {
        YansWifiPhyHelper accessPhy;
        YansWifiChannelHelper accessChannel = YansWifiChannelHelper::Default();
        accessChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel",
                                          "Speed",
                                          DoubleValue(delayMsToSpeed(wirelessDelay)));
        accessPhy.SetChannel(accessChannel.Create());

        NodeContainer leftPair;
        leftPair.Add(senders.Get(i));
        leftPair.Add(router1);
        senderLinkDevices[i] = wifi.Install(accessPhy, mac, leftPair);
    }

    YansWifiPhyHelper bottleneckPhy;
    YansWifiChannelHelper bottleneckChannel = YansWifiChannelHelper::Default();
    bottleneckChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel",
                                          "Speed",
                                          DoubleValue(delayMsToSpeed(bottleneckDelay)));
    bottleneckPhy.SetChannel(bottleneckChannel.Create());

    NodeContainer bottleneckPair;
    bottleneckPair.Add(router1);
    bottleneckPair.Add(router2);
    NetDeviceContainer bottleneckDevices = wifi.Install(bottleneckPhy, mac, bottleneckPair);

    for (uint32_t i = 0; i < nFlows; ++i)
    {
        YansWifiPhyHelper accessPhy;
        YansWifiChannelHelper accessChannel = YansWifiChannelHelper::Default();
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

    for (uint32_t i = 0; i < nFlows; ++i)
    {
        applyRandomLoss(senderLinkDevices[i]);
    }
    applyRandomLoss(bottleneckDevices);
    for (uint32_t i = 0; i < nFlows; ++i)
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

    std::cout << "Configured delays: wireless=" << wirelessDelay
              << " ms, bottleneck=" << bottleneckDelay << " ms" << std::endl;
    std::cout << "Configured bottleneck buffer: " << bottleneckBufferPkts
              << " packets" << std::endl;

    Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(receiverLinkDevices[0].Get(1));
    if (wifiDev)
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
    std::vector<Ipv4InterfaceContainer> senderLinkIfs(nFlows);
    std::vector<Ipv4InterfaceContainer> receiverLinkIfs(nFlows);

    for (uint32_t i = 0; i < nFlows; ++i)
    {
        std::string subnet = "10.1." + std::to_string(i + 1) + ".0";
        address.SetBase(subnet.c_str(), "255.255.255.0");
        senderLinkIfs[i] = address.Assign(senderLinkDevices[i]);
    }

    address.SetBase("10.1.100.0", "255.255.255.0");
    Ipv4InterfaceContainer bottleneckIfs = address.Assign(bottleneckDevices);

    for (uint32_t i = 0; i < nFlows; ++i)
    {
        std::string subnet = "10.1." + std::to_string(i + 11) + ".0";
        address.SetBase(subnet.c_str(), "255.255.255.0");
        receiverLinkIfs[i] = address.Assign(receiverLinkDevices[i]);
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    /* ======================
       Sink
       ====================== */

    std::vector<uint16_t> sinkPorts;
    ApplicationContainer sinkApps;
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        uint16_t sinkPort = static_cast<uint16_t>(8080 + i);
        sinkPorts.push_back(sinkPort);

        PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                    InetSocketAddress(Ipv4Address::GetAny(), sinkPort));

        ApplicationContainer sinkApp = sinkHelper.Install(receivers.Get(i));
        sinkApp.Start(Seconds(0.0));
        sinkApp.Stop(Seconds(20.0));
        sinkApps.Add(sinkApp);
    }

    /* ======================
       TCP Flow
       ====================== */

    std::vector<Ptr<TutorialApp>> apps;
    apps.reserve(nFlows);
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        Ptr<Socket> tcpSocket =
            Socket::CreateSocket(senders.Get(i), TcpSocketFactory::GetTypeId());

        tcpSocket->TraceConnectWithoutContext(
            "CongestionWindow",
            MakeBoundCallback(&CwndChangeWithFlow, i + 1));

        Address sinkAddress(InetSocketAddress(receiverLinkIfs[i].GetAddress(1), sinkPorts.at(i)));

        Ptr<TutorialApp> app = CreateObject<TutorialApp>();
        app->Setup(tcpSocket,
                   sinkAddress,
                   1000,
                   1000000,
                   DataRate("10Mbps"));

        senders.Get(i)->AddApplication(app);
        app->SetStartTime(Seconds(1.0 + 0.05 * i));
        app->SetStopTime(Seconds(20.0));
        apps.push_back(app);
    }

    Simulator::Stop(Seconds(20.0));
    Simulator::Run();

    double totalBytes = 0.0;
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(i));
        if (!sink)
        {
            continue;
        }

        double flowBytes = sink->GetTotalRx();
        totalBytes += flowBytes;
        std::cout << "Flow " << (i + 1) << " throughput (Mbps): "
                  << flowBytes * 8.0 / 1e6 / 20.0 << std::endl;
    }
    double throughputMbps = totalBytes * 8.0 / 1e6 / 20.0;

    std::cout << "Simulation complete." << std::endl;
    std::cout << "Average throughput (Mbps): "
              << throughputMbps << std::endl;

    Simulator::Destroy();

    return 0;
}