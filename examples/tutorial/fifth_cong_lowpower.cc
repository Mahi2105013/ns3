#include "tutorial-app.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/flow-monitor-helper.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/internet-module.h"
#include "ns3/ipv6-flow-classifier.h"
#include "ns3/lr-wpan-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/sixlowpan-module.h"

#include <iostream>
#include <unordered_map>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("PaperTopologyLrWpanMobile");

int
main(int argc, char* argv[])
{
    uint32_t nFlows = 3;
    double simulationTimeSeconds = 20.0;
    double nodeSpeed = 1.0;
    double packetsPerSecond = 100.0;
    double coverageSideMultiplier = 1.0;
    bool mobilityEnabled = true;
    std::string tcpType = "TcpNewReno";

    CommandLine cmd;
    cmd.AddValue("nFlows", "Number of TCP flows", nFlows);
    cmd.AddValue("tcpType", "TCP type (e.g., TcpNewReno, TcpVeno)", tcpType);
    cmd.AddValue("nodeSpeed", "Random walk speed in m/s", nodeSpeed);
    cmd.AddValue("packetsPerSecond", "Application packet rate (packets/s)", packetsPerSecond);
    cmd.AddValue("coverageSideMultiplier",
                 "Coverage side multiplier relative to the base grid spacing",
                 coverageSideMultiplier);
    cmd.AddValue("mobilityEnabled", "Enable random walk mobility (1) or static nodes (0)", mobilityEnabled);
    cmd.AddValue("simulationTime", "Simulation duration in seconds", simulationTimeSeconds);
    cmd.Parse(argc, argv);

    if (nFlows == 0)
    {
        std::cout << "nFlows must be > 0. Using 1." << std::endl;
        nFlows = 1;
    }

    if (nodeSpeed <= 0.0)
    {
        std::cout << "nodeSpeed must be > 0. Using 1.0." << std::endl;
        nodeSpeed = 1.0;
    }

    if (packetsPerSecond <= 0.0)
    {
        std::cout << "packetsPerSecond must be > 0. Using 100.0." << std::endl;
        packetsPerSecond = 100.0;
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

    if (simulationTimeSeconds <= 1.0)
    {
        std::cout << "simulationTime must be > 1.0. Using 20.0." << std::endl;
        simulationTimeSeconds = 20.0;
    }

    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::" + tcpType));
    Config::SetDefault("ns3::TcpSocket::InitialCwnd", UintegerValue(4));

    NodeContainer senders;
    senders.Create(nFlows);
    Ptr<Node> router1 = CreateObject<Node>();
    Ptr<Node> router2 = CreateObject<Node>();
    NodeContainer receivers;
    receivers.Create(nFlows);

    NodeContainer allNodes;
    allNodes.Add(senders);
    allNodes.Add(router1);
    allNodes.Add(router2);
    allNodes.Add(receivers);

    MobilityHelper mobility;
    const double baseSpacing = 8.0 * coverageSideMultiplier;
    mobility.SetPositionAllocator("ns3::GridPositionAllocator",
                                  "MinX",
                                  DoubleValue(0.0),
                                  "MinY",
                                  DoubleValue(0.0),
                                  "DeltaX",
                                  DoubleValue(baseSpacing),
                                  "DeltaY",
                                  DoubleValue(baseSpacing),
                                  "GridWidth",
                                  UintegerValue(4),
                                  "LayoutType",
                                  StringValue("RowFirst"));
    if (mobilityEnabled)
    {
        mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                                  "Mode",
                                  StringValue("Time"),
                                  "Time",
                                  TimeValue(Seconds(1.0)),
                                  "Speed",
                                  StringValue("ns3::ConstantRandomVariable[Constant=" +
                                              std::to_string(nodeSpeed) + "]"),
                                  "Bounds",
                                  RectangleValue(Rectangle(-2.5 * baseSpacing,
                                                           7.5 * baseSpacing,
                                                           -2.5 * baseSpacing,
                                                           7.5 * baseSpacing)));
    }
    else
    {
        mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    }
    mobility.Install(allNodes);

    LrWpanHelper lrWpanHelper;
    lrWpanHelper.SetPropagationDelayModel("ns3::ConstantSpeedPropagationDelayModel");
    lrWpanHelper.AddPropagationLossModel("ns3::LogDistancePropagationLossModel");
    NetDeviceContainer lrwpanDevices = lrWpanHelper.Install(allNodes);
    lrWpanHelper.CreateAssociatedPan(lrwpanDevices, 0);

    SixLowPanHelper sixLowPan;
    NetDeviceContainer sixLowPanDevices = sixLowPan.Install(lrwpanDevices);

    InternetStackHelper stack;
    stack.Install(allNodes);

    Ipv6AddressHelper ipv6;
    ipv6.SetBase(Ipv6Address("2001:1::"), Ipv6Prefix(64));
    Ipv6InterfaceContainer ifaces = ipv6.Assign(sixLowPanDevices);

    for (uint32_t i = 0; i < allNodes.GetN(); ++i)
    {
        ifaces.SetForwarding(i, true);
    }

    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll();

    std::vector<uint16_t> sinkPorts;
    ApplicationContainer sinkApps;
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        uint16_t sinkPort = static_cast<uint16_t>(9000 + i);
        sinkPorts.push_back(sinkPort);

        PacketSinkHelper sinkHelper("ns3::TcpSocketFactory",
                                    Inet6SocketAddress(Ipv6Address::GetAny(), sinkPort));
        ApplicationContainer sinkApp = sinkHelper.Install(receivers.Get(i));
        sinkApp.Start(Seconds(0.0));
        sinkApp.Stop(Seconds(simulationTimeSeconds));
        sinkApps.Add(sinkApp);
    }

    std::vector<Ptr<TutorialApp>> apps;
    apps.reserve(nFlows);
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        Ptr<Socket> tcpSocket = Socket::CreateSocket(senders.Get(i), TcpSocketFactory::GetTypeId());

        const uint32_t receiverGlobalIndex = nFlows + 2 + i;
        Address sinkAddress(Inet6SocketAddress(ifaces.GetAddress(receiverGlobalIndex, 1),
                                               sinkPorts[i]));

        Ptr<TutorialApp> app = CreateObject<TutorialApp>();
        const double appRateBps = packetsPerSecond * 100.0 * 8.0;
        app->Setup(tcpSocket,
                   sinkAddress,
                   100,
                   100000,
                   DataRate(std::to_string(static_cast<uint64_t>(appRateBps)) + "bps"));

        senders.Get(i)->AddApplication(app);
        app->SetStartTime(Seconds(1.0 + 0.1 * i));
        app->SetStopTime(Seconds(simulationTimeSeconds));
        apps.push_back(app);
    }

    Simulator::Stop(Seconds(simulationTimeSeconds));
    Simulator::Run();

    flowMonitor->CheckForLostPackets();

    double totalBytes = 0.0;
    std::vector<double> sinkThroughputMbps(nFlows, 0.0);
    std::vector<double> senderTxThroughputMbps(nFlows, 0.0);
    std::vector<double> receiverRxThroughputMbps(nFlows, 0.0);

    std::unordered_map<uint16_t, uint32_t> sinkPortToFlow;
    sinkPortToFlow.reserve(nFlows);
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        sinkPortToFlow.emplace(sinkPorts[i], i);
    }

    std::vector<double> flowDelayMs(nFlows, 0.0);
    std::vector<double> flowJitterMs(nFlows, 0.0);
    std::vector<double> flowLossRatio(nFlows, 0.0);
    std::vector<double> flowMonitorThroughputMbps(nFlows, 0.0);

    Ptr<Ipv6FlowClassifier> classifier = DynamicCast<Ipv6FlowClassifier>(flowHelper.GetClassifier());
    if (classifier)
    {
        const auto flowStats = flowMonitor->GetFlowStats();
        for (const auto& flow : flowStats)
        {
            Ipv6FlowClassifier::FiveTuple tuple = classifier->FindFlow(flow.first);
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
            senderTxThroughputMbps[idx] +=
                static_cast<double>(stat.txBytes) * 8.0 / 1e6 / simulationTimeSeconds;
            receiverRxThroughputMbps[idx] +=
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

    std::cout << "--- Per-flow QoS metrics (IPv6 FlowMonitor) ---" << std::endl;
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        std::cout << "Flow " << (i + 1)
                  << " fmThroughput(Mbps)=" << flowMonitorThroughputMbps[i]
                  << " avgDelay(ms)=" << flowDelayMs[i]
                  << " avgJitter(ms)=" << flowJitterMs[i]
                  << " lossRatio=" << flowLossRatio[i] << std::endl;
    }

    std::cout << "--- Per-node throughput summary (Mbps) ---" << std::endl;
    for (uint32_t i = 0; i < nFlows; ++i)
    {
        std::cout << "SenderNodeId=" << senders.Get(i)->GetId()
                  << " txThroughput=" << senderTxThroughputMbps[i]
                  << " | ReceiverNodeId=" << receivers.Get(i)->GetId()
                  << " rxThroughput=" << receiverRxThroughputMbps[i] << std::endl;
    }

    std::cout << "Simulation complete (802.15.4 + 6LoWPAN + mobile)." << std::endl;
    std::cout << "Average throughput (Mbps): " << throughputMbps << std::endl;

    Simulator::Destroy();
    return 0;
}
