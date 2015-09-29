using System;
using System.ComponentModel;
using System.Diagnostics;
using System.ServiceProcess;
using System.Net;
using System.Net.Sockets;
using System.Globalization;
using System.Security;

namespace PowerControlService
{
    public partial class ServicePowerControl : ServiceBase
    {

        #region constructor
        public ServicePowerControl()
        {
            InitializeComponent();
        }
        #endregion


        #region methods

        protected override void OnStart(string[] args)
        {
        }

        protected override void OnStop()
        {
        }

        private static SecureString MakeSecureString(string text)
        {
            SecureString secure = new SecureString();
            foreach (char c in text)
            {
                secure.AppendChar(c);
            }

            return secure;
        }

        internal static void shutDownRemoteComputer(string computerName)
        {

            ////http://stackoverflow.com/questions/734005/is-it-possible-to-shut-down-a-remote-pc-programatically-through-net-applicati
            //System.Diagnostics.Process proc = new System.Diagnostics.Process();
            //proc.EnableRaisingEvents = false;
            ////proc.StartInfo.UseShellExecute = false;
            //proc.StartInfo.FileName = "shutdown.exe";
            //proc.StartInfo.Arguments = @"\\"+computerName+@" -r -t 10 ";
            //proc.Start();

            //string PassWord = "MFGadmin01";

            Process process = new Process();
            ProcessStartInfo startInfo = new ProcessStartInfo();
            startInfo.FileName = "shutdown";//@"C:\Windows\System32\shutdown.exe";
            //startInfo.Password = MakeSecureString(PassWord);
            //startInfo.Domain = computerName;
            //startInfo.UserName = "MFGadmin";
            startInfo.Arguments = @"/s /f /t 5 /m \\" + computerName;
            startInfo.UseShellExecute = false;
            process.StartInfo = startInfo;
            process.Start();

        }

        internal static void wakeUpComputer(string MacAddress)
        {
            WOLClass WOL = new WOLClass();

            WOL.WakeFunction(MacAddress, "255.255.255.255");
        }

        internal static void wakeUpComputer(string MacAddress, string subNet)
        {
            WOLClass WOL = new WOLClass();

            WOL.WakeFunction(MacAddress, subNet);
        }
        #endregion
    }


    internal class WOLClass : UdpClient
    {
        public WOLClass()
            : base()
        { }
        //this is needed to send broadcast packet
        public void SetClientToBrodcastMode()
        {
            if (this.Active)
                this.Client.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.Broadcast, 0);
        }

        //now use this class
        //MAC_ADDRESS should  look like '013FA049'
        public void WakeFunction(string MAC_ADDRESS, string subNet)
        {
            //split subnet into AAA,BBB,CCC,DDD
            byte[] subNetBytes = new byte[4];
            string[] splitSubNetString = subNet.Trim().Split('.');
            for (int i = 0; i < 4; i++)
            {
                subNetBytes[i] = Convert.ToByte(splitSubNetString[i]);
            }

            IPAddress subnet = new IPAddress(subNetBytes);

            MAC_ADDRESS = MAC_ADDRESS.Replace("-", "");
            WOLClass client = new WOLClass();
            client.Connect(subnet,//new IPAddress(0xff1b3b95),//(0xffffffff),  //255.255.255.255  i.e broadcast
               0x2fff); // port=12287 let's use this one 
            client.SetClientToBrodcastMode();
            //set sending bites
            int counter = 0;
            //buffer to be send
            byte[] bytes = new byte[1024];   // more than enough :-)
            //first 6 bytes should be 0xFF
            for (int y = 0; y < 6; y++)
                bytes[counter++] = 0xFF;
            //now repeate MAC 16 times
            for (int y = 0; y < 16; y++)
            {
                int i = 0;
                for (int z = 0; z < 6; z++)
                {
                    bytes[counter++] =
                        byte.Parse(MAC_ADDRESS.Substring(i, 2), NumberStyles.HexNumber);
                    i += 2;
                }
            }

            //now send wake up packet
            int returned_value = client.Send(bytes, 1024);
        }
    }
}
