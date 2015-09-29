using System;
using System.Collections.Generic;
using System.Xml;
using System.Linq;
using System.Web;
using System.Web.Services;
using System.Net.Mail;
using System.Web.Configuration;
using System.Net;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Configuration;

namespace RemotePower
{
    /// <summary>
    /// Summary description for WebServiceRemotePower
    /// </summary>
    [WebService(Namespace = "http://tempuri.org/")]
    [WebServiceBinding(ConformsTo = WsiProfiles.BasicProfile1_1)]
    [System.ComponentModel.ToolboxItem(false)]
    // To allow this Web Service to be called from script, using ASP.NET AJAX, uncomment the following line. 
    // [System.Web.Script.Services.ScriptService]
    public class WebServiceRemotePower : System.Web.Services.WebService
    {
        private string remoteShutDownPassword = "";

        //remoteShutDownPassword = ConfigurationManager.AppSettings["remoteShutDownPassword"].ToString();
        string AndonMapXMLFile = HttpContext.Current.Server.MapPath("~/App_Data/XMLFilePowerControlComputers.xml");
        string HQNTemplateFile = HttpContext.Current.Server.MapPath("~/App_Data/XMLFilePowerControlComputers.xml");
        

        [WebMethod]
        public void TurnOnGroup(string ComputerGroup)
        {
            XmlDocument compList = new XmlDocument();
            compList.Load(AndonMapXMLFile);

            string expression = @"//root/computerGroup[name='" + ComputerGroup + @"']/member";
            XmlNodeList varList = compList.SelectNodes(expression);

            foreach (XmlNode node in varList)
            {
                string memberType = node.SelectSingleNode(@"./type").InnerText;
                switch (memberType)
                {
                    case "computer":
                        TurnOnComputerPower(node.SelectSingleNode("macAddress").InnerText, compList.SelectSingleNode(@"//root/computerGroup[name='" + ComputerGroup + @"']/subnet").InnerText);
                        break;
                    case "group":
                        TurnOnGroup(node.SelectSingleNode("macAddress").InnerText);
                        break;
                    default:
                        break;
                }
            }

        }

        [WebMethod]
        public void TurnOffGroup(string ComputerGroup, string PassWord)
        {

            XmlDocument compList = new XmlDocument();
            compList.Load(AndonMapXMLFile);

            string expression = @"//root/computerGroup[name='"+ComputerGroup+@"']/member";
            XmlNodeList varList = compList.SelectNodes(expression);
            
            foreach (XmlNode node in varList)
            {
                string memberType = node.SelectSingleNode(@"./type").InnerText;
                switch (memberType)
                {
                    case "computer":
                        TurnOffComputerPower(node.SelectSingleNode("name").InnerText, PassWord);
                        break;
                    case "group":
                        TurnOffGroup(node.SelectSingleNode("name").InnerText, PassWord);
                        break;
                    default:
                        break;
                }
            }

        }

        [WebMethod]
        public void TurnOnLights()
        {
            string URL = string.Format(WebConfigurationManager.AppSettings["productionFloorLightURL"].ToString());

            using (var client = new WebClient())
            {
                var response = new WebClient().DownloadString(URL);
            }

        }

        [WebMethod]
        public void TurnOnComputerPower(string MacAddress, string SubNet)
        {

            RemotePowerControl.wakeUpComputer(MacAddress, SubNet);

        }

        [WebMethod]
        public void TurnOffComputerPower(string ComputerName, string PassWord)
        {
            //check that password is valid
            //if (PassWord != remoteShutDownPassword)
            //{
            //    throw new Exception("invalid password for shutdown");
            //}

            //            RemotePowerControl.shutDownRemoteComputer(ComputerName);
            
            RemotePowerControl.shutDownRemoteComputer(ComputerName);
        }

        [WebMethod]
        public void AndonListener(string ClientMacAddress, string ControlName, string ControlState)
        {
            //look up group(s) associated with a controller by its MAC address in the XML file
            XmlDocument compList = new XmlDocument();
            compList.Load(AndonMapXMLFile);

            //determine the action to take based on the client, the control and the state
            //string expression = @"//root/computerGroup[member/type = 'group controller' and member/macAddress = '" + ClientMacAddress + "']";
            string expression = @"//root/computerGroup[member/type = 'group controller'";
            expression += " and member/macAddress = '" + ClientMacAddress + "'";
            expression += " and member/control/name = '" + ControlName + "'";
            expression += " and member/control/state/name = '" + ControlState + "']";

            XmlNodeList varList = compList.SelectNodes(expression);

            
            //action = node.SelectSingleNode("name").InnerText;
            //loop through each group and determine the action(s) to take
            foreach (XmlNode node in varList)
            {
                string groupName = node.SelectSingleNode("name").InnerText;
                //string controllerName = node.SelectSingleNode("").InnerText;
                //Write a record to the database for each group that is signalled (probably one group)
                WriteToHQN(ClientMacAddress, ControlName, ControlState, groupName);

                //loop through each action for the group
                string actionExpression = @"member[type = 'group controller'";
                actionExpression += @" and macAddress = '" + ClientMacAddress + "']";
                actionExpression += @"/control[name = '" + ControlName + "']";
                actionExpression += @"/state[name = '" + ControlState + "']/action";

                XmlNodeList actionList = node.SelectNodes(actionExpression);
                foreach (XmlNode nd in actionList)
                {
                    string action = nd.InnerText;


                    //foreach action for that client/control/state
                    if (action == "turn on group")
                    {
                        //
                        TurnOnGroup(groupName);
                    }
                    else if (action == "turn off group")
                    {
                        //
                        TurnOffGroup(groupName, remoteShutDownPassword);
                    }
                    else if (action == "turn on lights")
                    {
                        //call the method to turn on the production floor lights
                        TurnOnLights();
                    }
                    else if (action.StartsWith("email"))
                    {
                        MailMessage message = new MailMessage();

                        //send email to recipient
                        //split string
                        string[] splitAction = action.Split(':');
                        message.To.Add(new MailAddress(splitAction[1]));

                        //email from: is set in web.config

                        //email subject:
                        message.Subject = groupName + ": " + ControlName + ", " + ControlState;

                        //email body left blank
                        message.Body = ConfigurationManager.AppSettings["EmailBody"];

                        //send email
                        SmtpClient client = new SmtpClient();
                        //set the timeout property in milliseconds.  Default is 100,000
                        client.Timeout = 20000;
                        try
                        {
                            client.Send(message);
                        }
                        catch (Exception ex)
                        {
                            //writeExceptionToEventLog(ex);
                            throw;
                        }
                    }

                    else throw new Exception("unrecognized 'action' input");
                }
            }
        }

        private void WriteToHQN(string ClientMacAddress, string ControlName, string ControlState, string ComputerGroup)
        {
            //create the XML based on LC0095-001
            XmlDocument HQNFile = new XmlDocument();

            XmlDeclaration xmlDeclaration = HQNFile.CreateXmlDeclaration("1.0", "UTF-8", null);
            XmlElement root = HQNFile.DocumentElement;
            HQNFile.InsertBefore(xmlDeclaration, root);

            //create the root element
            XmlElement rootElement = HQNFile.CreateElement(string.Empty, "hqn", string.Empty);
            HQNFile.AppendChild(rootElement);

            //create the event element
            XmlElement eventElement = HQNFile.CreateElement(string.Empty, "event", string.Empty);
            rootElement.AppendChild(eventElement);

            //Create the event id by converting the dateTime to int32
            //XmlElement eventIdElement = HQNFile.CreateElement(string.Empty, "event_id", string.Empty);
            //eventIdElement.AppendChild(HQNFile.CreateTextNode(unchecked((int)DateTime.Now.Ticks).ToString()));
            //eventElement.AppendChild(eventIdElement);

            //Write the date
            XmlElement dateTimeElement = HQNFile.CreateElement(string.Empty, "date_time", string.Empty);
            dateTimeElement.AppendChild(HQNFile.CreateTextNode(DateTime.Now.ToString("MM/dd/yyyy HH:mm:ss")));
            eventElement.AppendChild(dateTimeElement);

            //Write the computer group as client, e.g. HS1/FRx FAT, line 1, etc.
            //maybe in the future add a hook to specify a controller from the group
            XmlElement clientElement = HQNFile.CreateElement(string.Empty, "client", string.Empty);
            clientElement.AppendChild(HQNFile.CreateTextNode(ComputerGroup));
            eventElement.AppendChild(clientElement);

            //Write a web.config entry for supplier
            XmlElement supplierElement = HQNFile.CreateElement(string.Empty, "supplier", string.Empty);
            supplierElement.AppendChild(HQNFile.CreateTextNode(ConfigurationManager.AppSettings["HQNSupplier"]));
            eventElement.AppendChild(supplierElement);

            //Write a web.config entry for product.  Product has no meaning in this context
            XmlElement productElement = HQNFile.CreateElement(string.Empty, "product", string.Empty);
            productElement.AppendChild(HQNFile.CreateTextNode(ConfigurationManager.AppSettings["HQNProduct"]));
            eventElement.AppendChild(productElement);

            //Write the process name
            XmlElement processElement = HQNFile.CreateElement(string.Empty, "process", string.Empty);
            processElement.AppendChild(HQNFile.CreateTextNode("Andon Monitor"));
            eventElement.AppendChild(processElement);

            //Write an empty string for serial number as a default
            XmlElement serialNumberElement = HQNFile.CreateElement(string.Empty, "serial_number", string.Empty);
            serialNumberElement.AppendChild(HQNFile.CreateTextNode(ComputerGroup));
            eventElement.AppendChild(serialNumberElement);

            //write True final status as a default
            XmlElement finalStatusElement = HQNFile.CreateElement(string.Empty, "final_status", string.Empty);
            finalStatusElement.AppendChild(HQNFile.CreateTextNode("True"));
            eventElement.AppendChild(finalStatusElement);

            //<creation_date>5/1/2015 5:35:40 PM</creation_date>
            ////Write the date
            //XmlElement creationDateTimeElement = HQNFile.CreateElement(string.Empty, "creation_date", string.Empty);
            ////re-use dateTimeElement
            //eventElement.AppendChild(dateTimeElement);

            ////<file_created>False</file_created>
            ////write True final status as a default
            //XmlElement fileCreatedElement = HQNFile.CreateElement(string.Empty, "file_created", string.Empty);
            //fileCreatedElement.AppendChild(HQNFile.CreateTextNode("True"));
            //eventElement.AppendChild(fileCreatedElement);
                        
            //create the first attribute element for the control name
            XmlElement attributeElementControl = HQNFile.CreateElement(string.Empty, "attribute", string.Empty);
            rootElement.AppendChild(attributeElementControl);

            //write the control name
            XmlElement attributeNameElementControlName = HQNFile.CreateElement(string.Empty, "attr_name", string.Empty);
            attributeNameElementControlName.AppendChild(HQNFile.CreateTextNode(ControlName));
            attributeElementControl.AppendChild(attributeNameElementControlName);

            //write the control state
            XmlElement attributeNameElementControlState = HQNFile.CreateElement(string.Empty, "attr_value", string.Empty);
            attributeNameElementControlState.AppendChild(HQNFile.CreateTextNode(ControlState));
            attributeElementControl.AppendChild(attributeNameElementControlState);


            //create the second attribute element for the mac address of the controller (client)
            XmlElement attributeElementMACAddress = HQNFile.CreateElement(string.Empty, "attribute", string.Empty);
            rootElement.AppendChild(attributeElementMACAddress);

            //write a mac address attribute
            XmlElement attributeNameElementMacName = HQNFile.CreateElement(string.Empty, "attr_name", string.Empty);
            attributeNameElementMacName.AppendChild(HQNFile.CreateTextNode("MAC address"));
            attributeElementMACAddress.AppendChild(attributeNameElementMacName);

            //write the client mac address as the value
            XmlElement attributeNameElementMacValue = HQNFile.CreateElement(string.Empty, "attr_value", string.Empty);
            attributeNameElementMacValue.AppendChild(HQNFile.CreateTextNode(ClientMacAddress));
            attributeElementMACAddress.AppendChild(attributeNameElementMacValue);


            //create the second attribute element for the mac address of the controller (client)
            XmlElement attributeElementGuid = HQNFile.CreateElement(string.Empty, "attribute", string.Empty);
            rootElement.AppendChild(attributeElementGuid);

            //write a mac address attribute
            XmlElement attributeNameElementGuidName = HQNFile.CreateElement(string.Empty, "attr_name", string.Empty);
            attributeNameElementGuidName.AppendChild(HQNFile.CreateTextNode("GUID"));
            attributeElementGuid.AppendChild(attributeNameElementGuidName);

            //write the client mac address as the value
            XmlElement attributeNameElementGuidValue = HQNFile.CreateElement(string.Empty, "attr_value", string.Empty);
            attributeNameElementGuidValue.AppendChild(HQNFile.CreateTextNode(Guid.NewGuid().ToString()));
            attributeElementGuid.AppendChild(attributeNameElementGuidValue);

            //create a file path with the local file path, machine name, and date time
            string HqnFileName = ConfigurationManager.AppSettings["HQNFileLocalPath"];
            HqnFileName += Server.MachineName + "_" + DateTime.Now.ToString("yyyyMMddHHmmss")+"_andon_monitor.xml";
            HQNFile.Save(HqnFileName);

            //send XML to HQN file drop along with any other files
            MoveHQNFiles();
        }

        private void MoveHQNFiles()
        {
            int waitForLockedFileSeconds = Int32.Parse(ConfigurationManager.AppSettings["waitForLockedFileSeconds"]);
            string HQNFileDrop = ConfigurationManager.AppSettings["HQNFileDrop"];
            //string HQNFileLocalPath = ConfigurationManager.AppSettings["HQNFileLocalPath"];

            DirectoryInfo folder = new DirectoryInfo(ConfigurationManager.AppSettings["HQNFileLocalPath"]);

            foreach (FileInfo _file in folder.GetFiles())
            {
                try
                {
                    //if file exists (not a folder)
                    if (_file.Exists)
                    {
                        //in this method, check if the file is locked and process
                        //if the file is still locked after wait, throw exception and don't touch file
                        if (IsFileLocked(_file))
                        {
                            //wait for an amount of time from config file
                            Thread.Sleep(TimeSpan.FromSeconds(waitForLockedFileSeconds));

                        }
                        if (IsFileLocked(_file))
                        {
                            throw new Exception("File is locked and exceeded wait time");
                        }

                        string destinationFilePath = ConfigurationManager.AppSettings["HQNFileDrop"] + _file.Name;

                        //move file to output directory
                        //moveFile(_file.FullName, destinationFilePath);
                        _file.MoveTo(destinationFilePath);

                    }
                    //else file does not exist.  this is if the returned name is folder, other conditions(?)
                }
                catch (Exception ex)
                {
                    //writeExceptionToEventLog(ex);
                }

            }
        }

        protected void writeExceptionToEventLog(Exception ex)
        {
            string mySource = "Andon Web Services";

            // Create the source, if it does not already exist.
            if (!EventLog.SourceExists(mySource))
            {
                EventLog.CreateEventSource(mySource, "Application");
            }

            // Create an EventLog instance and assign its source.
            EventLog myLog = new EventLog();
            myLog.Source = mySource;

            // Write an informational entry to the event log.    
            myLog.WriteEntry(ex.Message);

            myLog.Close();
        }

        protected virtual bool IsFileLocked(FileInfo file)
        {
            //http://stackoverflow.com/questions/876473/is-there-a-way-to-check-if-a-file-is-in-use
            FileStream stream = null;

            try
            {
                stream = file.Open(FileMode.Open, FileAccess.ReadWrite, FileShare.None);
            }
            catch (IOException)
            {
                //the file is unavailable because it is:
                //still being written to
                //or being processed by another thread
                //or does not exist (has already been processed)
                return true;
            }
            finally
            {
                if (stream != null)
                    stream.Close();
            }

            //file is not locked
            return false;
        }

    }
}
