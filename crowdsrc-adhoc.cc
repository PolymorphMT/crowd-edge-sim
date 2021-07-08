/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2005,2006,2007 INRIA
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Mathieu Lacage <mathieu.lacage@sophia.inria.fr>
 * 
 * NOTE FOR DISSERTATION PROJECT BY MARK TYLER
 * ---
 * I have left a few things in the script, commented out, to reflect some of
 * the approaches that I took in the development of the script but ultimately
 * had to leave out. These may or may not have been mentioned in the dissertation
 * text.
 */

#include "ns3/gnuplot.h"
#include "ns3/command-line.h"
#include "ns3/config.h"
#include "ns3/uinteger.h"
#include "ns3/double.h"
#include "ns3/string.h"
#include "ns3/log.h"
#include "ns3/yans-wifi-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/ipv6-address-helper.h"
#include "ns3/on-off-helper.h"
#include "ns3/yans-wifi-channel.h"
#include "ns3/mobility-model.h"
#include "ns3/packet-socket-helper.h"
#include "ns3/packet-socket-address.h"
#include "ns3/netanim-module.h"
#include "ns3/ripng-helper.h"
#include "ns3/ipv6-static-routing-helper.h"
#include "ns3/ipv6-list-routing-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/flow-monitor-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("Wifi-Adhoc");

class Experiment
{
public:
  Experiment ();
  Experiment (std::string name);
  Gnuplot2dDataset Run (const WifiHelper &wifi, const YansWifiPhyHelper &wifiPhy,
                        const WifiMacHelper &wifiMac, const YansWifiChannelHelper &wifiChannel);
private:
  void ReceivePacket (Ptr<Socket> socket);
  Ptr<Socket> SetupPacketReceive (Ptr<Node> node);

  uint32_t m_bytesTotal;
  Gnuplot2dDataset m_output;
};

Experiment::Experiment ()
{
}

Experiment::Experiment (std::string name)
  : m_output (name)
{
  m_output.SetStyle (Gnuplot2dDataset::LINES);
}

static void
CourseChange (std::string context, Ptr<const MobilityModel> mobility)
 {
   Vector pos = mobility->GetPosition ();
   Vector vel = mobility->GetVelocity ();
   std::cout << Simulator::Now () << ", model=" << mobility << ", POS: x=" << pos.x << ", y=" << pos.y
             << ", z=" << pos.z << "; VEL:" << vel.x << ", y=" << vel.y
             << ", z=" << vel.z << std::endl;
 }

/* 
* In this method, I had plans to add more functionality with
* regards to forwarding packets between nodes and simulating
* the sharing of data and not just plain packets.
*/
void
Experiment::ReceivePacket (Ptr<Socket> socket)
{
  Ptr<Packet> packet;
  while ((packet = socket->Recv ()))
    {
      SocketIpv6HopLimitTag hoplimitTag;
      if (packet->RemovePacketTag (hoplimitTag))
        {
          NS_LOG_INFO (" HOPLIMIT = " << (uint32_t)hoplimitTag.GetHopLimit ());
        }
      m_bytesTotal += packet->GetSize ();
    }
}

Ptr<Socket>
Experiment::SetupPacketReceive (Ptr<Node> node)
{
  TypeId tid = TypeId::LookupByName ("ns3::PacketSocketFactory");
  Ptr<Socket> sink = Socket::CreateSocket (node, tid);
  NS_LOG_UNCOND ("socket created");
  uint32_t ipv6Hoplimit = 0;
  bool ipv6RecvHoplimit = true;
  sink->SetIpv6RecvHopLimit (ipv6RecvHoplimit);
  sink->Bind ();
  if (ipv6Hoplimit > 3)
    {
      sink->SetIpv6HopLimit (ipv6Hoplimit);
    }
  sink->SetRecvCallback (MakeCallback (&Experiment::ReceivePacket, this));
  NS_LOG_UNCOND ("sink made");
  return sink;
}

Gnuplot2dDataset
Experiment::Run (const WifiHelper &wifi, const YansWifiPhyHelper &wifiPhy,
                 const WifiMacHelper &wifiMac, const YansWifiChannelHelper &wifiChannel)
{
  Config::SetDefault ("ns3::RandomWalk2dMobilityModel::Mode", StringValue ("Time"));
  Config::SetDefault ("ns3::RandomWalk2dMobilityModel::Time", StringValue ("2s"));
  Config::SetDefault ("ns3::RandomWalk2dMobilityModel::Speed", StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"));
  Config::SetDefault ("ns3::RandomWalk2dMobilityModel::Bounds", StringValue ("0|200|0|200"));

  m_bytesTotal = 0;

  NodeContainer c;
  c.Create (20);

  YansWifiPhyHelper phy = wifiPhy;
  phy.SetChannel (wifiChannel.Create ());

  WifiMacHelper mac = wifiMac;
  NetDeviceContainer devices = wifi.Install (phy, mac, c);

  MobilityHelper mobility;
  mobility.SetPositionAllocator ("ns3::RandomDiscPositionAllocator",
                                  "X", StringValue ("100.0"),
                                  "Y", StringValue ("100.0"),
                                  "Rho", StringValue ("ns3::UniformRandomVariable[Min=0|Max=30]"));
  mobility.SetMobilityModel ("ns3::RandomWalk2dMobilityModel",
                              "Mode", StringValue ("Time"),
                              "Time", StringValue ("2s"),
                              "Speed", StringValue ("ns3::ConstantRandomVariable[Constant=2.0]"),
                              "Bounds", StringValue ("0|200|0|200"));

  mobility.Install (c);
  Config::Connect ("/NodeList/*/$ns3::MobilityModel/CourseChange",
                    MakeCallback (&CourseChange));


  RipNgHelper ripNg;
  Ipv6StaticRoutingHelper staticRouting;

  Ipv6ListRoutingHelper list;
  list.Add (staticRouting, 0);
  list.Add (ripNg, 10);

  InternetStackHelper internet;
  internet.SetRoutingHelper (list); // has effect on the next Install ()
  internet.Install (c);

  Ipv6AddressHelper ipv6;
  NS_LOG_INFO ("Assign IP Addresses.");
  ipv6.SetBase (Ipv6Address ("2001:1::"), Ipv6Prefix (64));
  Ipv6InterfaceContainer i = ipv6.Assign (devices);

  Inet6SocketAddress socket = Inet6SocketAddress (i.GetAddress (0, 1));

  OnOffHelper onoff ("ns3::PacketSocketFactory", Address (socket));
  onoff.SetConstantRate (DataRate (60000000));
  onoff.SetAttribute ("PacketSize", UintegerValue (4096));

  Ptr<FlowMonitor> flowMonitor;
  FlowMonitorHelper flowHelper;
  flowMonitor = flowHelper.InstallAll();

  ApplicationContainer apps = onoff.Install (c.Get (0));
  apps.Start (Seconds (0.5));
  apps.Stop (Seconds (300.0));
  
  NS_LOG_UNCOND ("socket receive");
  
  Ptr<Socket> recvSink = SetupPacketReceive (c.Get (1));
  AnimationInterface anim ("animation.xml");
  anim.EnablePacketMetadata (true);
  phy.EnablePcap ("crowdsrc-adhoc", devices);
  NS_LOG_UNCOND ("run");
  Simulator::Stop (Seconds (600.0));
  Simulator::Run ();

  NS_LOG_UNCOND ("destroy");
  flowMonitor->SerializeToXmlFile ("crowdsrc-adhoc-flow.xml", true, true);
  Simulator::Destroy ();

  return m_output;
}

int main (int argc, char *argv[])
{
  CommandLine cmd;
  cmd.Parse (argc, argv);

  Gnuplot gnuplot = Gnuplot ("reference-rates.png");

  /* 
  * In this section I had hoped to add more complex features
  * of WiFi transmission such as noise, delay, loss, etc. These
  * were ultimately left out as the simulation would run but packets
  * would not be sent. I had not worked out the cause as I did not
  * deem it of high priority but I think it would have helped the quality
  * of the project if added in.
   */

  Experiment experiment;
  WifiHelper wifi;
  wifi.SetStandard (WIFI_PHY_STANDARD_80211a);
  //wifi.EnableLogComponents ();  // Turn on all Wifi logging
  WifiMacHelper wifiMac;
  YansWifiPhyHelper wifiPhy = YansWifiPhyHelper::Default ();
  wifiPhy.Set ("RxGain", DoubleValue (-10) );
  wifiPhy.SetPcapDataLinkType (WifiPhyHelper::DLT_IEEE802_11_RADIO);
  YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default ();
  //wifiChannel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
  //wifiChannel.AddPropagationLoss ("ns3::FriisPropagationLossModel");
  wifiPhy.SetChannel (wifiChannel.Create ());
  //double txp = 7.5;
  //wifiPhy.Set ("TxPowerStart",DoubleValue (txp));
  //wifiPhy.Set ("TxPowerEnd", DoubleValue (txp));
  Gnuplot2dDataset dataset;

  wifiMac.SetType ("ns3::AdhocWifiMac");

  /* 
  * The following code was taken from the wifi-adhoc.cc script, and
  * was used for debugging as I would test the script against these
  * different settings. In the final version, I thought they were superfluous
  * as only one setting would produce an animation and trace files for analysis,
  * so I opted for one, which was the Ideal setting. I have left these in for
  * context with my testing.
   */


  /*NS_LOG_DEBUG ("54");
  experiment = Experiment ("54mb");
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue ("OfdmRate54Mbps"));                  
  dataset = experiment.Run (wifi, wifiPhy, wifiMac, wifiChannel);
  gnuplot.AddDataset (dataset);

  NS_LOG_DEBUG ("48");
  experiment = Experiment ("48mb");
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue ("OfdmRate48Mbps"));
  dataset = experiment.Run (wifi, wifiPhy, wifiMac, wifiChannel);
  gnuplot.AddDataset (dataset);

  NS_LOG_DEBUG ("36");
  experiment = Experiment ("36mb");
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue ("OfdmRate36Mbps"));
  dataset = experiment.Run (wifi, wifiPhy, wifiMac, wifiChannel);
  gnuplot.AddDataset (dataset);

  NS_LOG_DEBUG ("24");
  experiment = Experiment ("24mb");
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue ("OfdmRate24Mbps"));
  dataset = experiment.Run (wifi, wifiPhy, wifiMac, wifiChannel);
  gnuplot.AddDataset (dataset);

  NS_LOG_DEBUG ("18");
  experiment = Experiment ("18mb");
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue ("OfdmRate18Mbps"));
  dataset = experiment.Run (wifi, wifiPhy, wifiMac, wifiChannel);
  gnuplot.AddDataset (dataset);

  NS_LOG_DEBUG ("12");
  experiment = Experiment ("12mb");
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue ("OfdmRate12Mbps"));
  dataset = experiment.Run (wifi, wifiPhy, wifiMac, wifiChannel);
  gnuplot.AddDataset (dataset);

  NS_LOG_DEBUG ("9");
  experiment = Experiment ("9mb");
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue ("OfdmRate9Mbps"));
  dataset = experiment.Run (wifi, wifiPhy, wifiMac, wifiChannel);
  gnuplot.AddDataset (dataset);

  NS_LOG_DEBUG ("6");
  experiment = Experiment ("6mb");
  wifi.SetRemoteStationManager ("ns3::ConstantRateWifiManager",
                                "DataMode", StringValue ("OfdmRate6Mbps"));
  dataset = experiment.Run (wifi, wifiPhy, wifiMac, wifiChannel);
  gnuplot.AddDataset (dataset);

  gnuplot.GenerateOutput (std::cout);

  gnuplot = Gnuplot ("rate-control.png");
  wifi.SetStandard (WIFI_PHY_STANDARD_holland);

  NS_LOG_DEBUG ("arf");
  experiment = Experiment ("arf");
  wifi.SetRemoteStationManager ("ns3::ArfWifiManager");
  dataset = experiment.Run (wifi, wifiPhy, wifiMac, wifiChannel);
  gnuplot.AddDataset (dataset);

  NS_LOG_DEBUG ("aarf");
  experiment = Experiment ("aarf");
  wifi.SetRemoteStationManager ("ns3::AarfWifiManager");
  dataset = experiment.Run (wifi, wifiPhy, wifiMac, wifiChannel);
  gnuplot.AddDataset (dataset);

  NS_LOG_DEBUG ("aarf-cd");
  experiment = Experiment ("aarf-cd");
  wifi.SetRemoteStationManager ("ns3::AarfcdWifiManager");
  dataset = experiment.Run (wifi, wifiPhy, wifiMac, wifiChannel);
  gnuplot.AddDataset (dataset);

  NS_LOG_DEBUG ("cara");
  experiment = Experiment ("cara");
  wifi.SetRemoteStationManager ("ns3::CaraWifiManager");
  dataset = experiment.Run (wifi, wifiPhy, wifiMac, wifiChannel);
  gnuplot.AddDataset (dataset);

  NS_LOG_DEBUG ("rraa");
  experiment = Experiment ("rraa");
  wifi.SetRemoteStationManager ("ns3::RraaWifiManager");
  dataset = experiment.Run (wifi, wifiPhy, wifiMac, wifiChannel);
  gnuplot.AddDataset (dataset);*/

  NS_LOG_DEBUG ("ideal");
  experiment = Experiment ("ideal");
  wifi.SetRemoteStationManager ("ns3::IdealWifiManager");
  dataset = experiment.Run (wifi, wifiPhy, wifiMac, wifiChannel);
  gnuplot.AddDataset (dataset);

  gnuplot.GenerateOutput (std::cout);

  return 0;
}
