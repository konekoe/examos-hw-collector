#ifdef __cplusplus
extern "C"{
#endif

#include <cstdint>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#ifndef PCI_ABI
#define PCI_ABI
#endif

struct pci_access *pci_alloc(void) PCI_ABI;
void pci_init(struct pci_access *) PCI_ABI;
void pci_cleanup(struct pci_access *) PCI_ABI;
void pci_scan_bus(struct pci_access *acc) PCI_ABI;
int pci_fill_info(struct pci_dev *, int flags) PCI_ABI;
u8 pci_read_byte(struct pci_dev *, int pos) PCI_ABI;
char *pci_lookup_name(struct pci_access *a, char *buf, int size, int flags, ...) PCI_ABI;

#ifdef __cplusplus
}
#endif


#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/process.hpp>
#include <boost/optional/optional_io.hpp>
#include <cstdlib>
#include <iostream>
#include <string>
#include <cstdio>

#include "pci/pci.h"

#include "json.hpp"
using json = nlohmann::json;


int main(void)
{

  /*
   * This is the main json object we are filling with hardware/bootup information.
   * It will be filled with different objects, named after tools/logs which they represent
   */
  json info;

  /*
   * First we query the pci devices sorted by their classes
   * They are then inserted into arrays under JSON objects 
   * named after their classes.
   */
  struct pci_access *pacc;

  pacc = pci_alloc();		/* Get the pci_access structure */
  pci_init(pacc);		/* Initialize the PCI library */
  pci_scan_bus(pacc);		/* We want to get the list of devices */

  for (struct pci_dev *dev = pacc->devices; dev; dev = dev->next)	/* Iterate over all devices */
  {
    char classbuf[1024], vendorbuf[1024], devicebuf[1024];
    pci_fill_info(dev, PCI_FILL_IDENT | PCI_FILL_BASES | PCI_FILL_CLASS);	/* Fill in header info we need */

    /* Look up and print the full name of the device */
    pci_lookup_name(pacc, classbuf, sizeof(classbuf), PCI_LOOKUP_NETWORK | PCI_LOOKUP_CACHE | PCI_LOOKUP_CLASS, dev->device_class);
    pci_lookup_name(pacc, vendorbuf, sizeof(vendorbuf), PCI_LOOKUP_NETWORK | PCI_LOOKUP_CACHE | PCI_LOOKUP_VENDOR, dev->vendor_id, dev->device_id);
    pci_lookup_name(pacc, devicebuf, sizeof(devicebuf), PCI_LOOKUP_NETWORK | PCI_LOOKUP_CACHE | PCI_LOOKUP_DEVICE, dev->vendor_id, dev->device_id);
    try
    {
      info["lspci"][classbuf].emplace_back(json::object({ { "vendor", vendorbuf }, { "device", devicebuf } }));
    }
    catch(const std::exception& e)
    {
      // fall through
    }
  }
  pci_cleanup(pacc);		/* Close everything pci related */


  /*
   * Now we query the dmidecode output and 
   * convert it into JSON data using a bash script
   * (dmi2json). It is then added to its own JSON object 
   * inside the info object.
   */
  try
  {
    boost::process::pipe process_pipe;
    boost::process::ipstream process_ipstream;

    boost::process::child dmidecode(boost::process::search_path("dmidecode"),  boost::process::std_out > process_pipe);
    boost::process::child dmi2json(boost::process::search_path("dmi2json"), boost::process::std_in < process_pipe, boost::process::std_out > process_ipstream);

    std::string line;
    std::stringstream output;
    while (dmi2json.running() && std::getline(process_ipstream, line))
          output << line;

    info["dmidecode"] = json::parse(output.str());
    output.clear();
    line.clear();
  }
  catch(const std::exception& e)
  {
    /* If our own parsing failed, we can always use the raw output too :-) */
    try
    {
      boost::process::ipstream process_ipstream;
      boost::process::child dmidecode(boost::process::search_path("dmidecode"), boost::process::std_out > process_ipstream);

      std::string line;
      std::stringstream output;
      while (dmidecode.running() && std::getline(process_ipstream, line))
            output << line << std::endl;

      info["dmidecode"] = output.str();
      output.clear();
      line.clear();
    }
    catch(const std::exception& e)
    {
      info["dmidecode"] = "COULD NOT BE PARSED";
    }
  }
  
  


  /*
   * Include dmesg output as raw text
   */
  try
  {
    boost::process::ipstream process_ipstream;
    boost::process::child dmesg(boost::process::search_path("dmesg"), "--color=never", boost::process::std_out > process_ipstream);

    std::string line;
    std::stringstream output;
    while (dmesg.running() && std::getline(process_ipstream, line))
          output << line << std::endl;

    info["dmesg"] = output.str();
    output.clear();
    line.clear();
  }
  catch(const std::exception& e)
  {
    info["dmesg"] = "COULD NOT BE PARSED";
  }
  
  
  

  /*
   * Include journalctl output as raw text
   */
  try
  {
    boost::process::ipstream process_ipstream;
    boost::process::child journalctl(boost::process::search_path("journalctl"), "-b", /*"-o", "json",*/ boost::process::std_out > process_ipstream);

    std::string line;
    std::stringstream output;
    while (journalctl.running() && std::getline(process_ipstream, line))
          output << line << std::endl;

    info["journalctl"] = output.str();
    output.clear();
  }
  catch(const std::exception& e)
  {
    info["journalctl"] = "COULD NOT BE PARSED";
  }
  
  


  /*
   * Last, we connect to the HTTPS server and create 
   * a POST request containing the collected data as a 
   * JSON payload and send it. This is tried as long as 
   * a successful request have been made.
   */
  while(true)
  {
    /* Get some rest */
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
    try
    {
      auto host = "srv.examos.aalto.fi";
      auto port = "5000";

      boost::asio::io_context ioc;

      boost::asio::ssl::context ctx{boost::asio::ssl::context::sslv23_client};

      ctx.set_verify_mode(boost::asio::ssl::verify_none);

      boost::asio::ip::tcp::resolver resolver{ioc};
      boost::asio::ssl::stream<boost::asio::ip::tcp::socket> stream{ioc, ctx};

      if(! SSL_set_tlsext_host_name(stream.native_handle(), host))
      {
        return -1;
      }

      /* Resolve connection and do SSL handshake */
      auto const results = resolver.resolve(host, port);
      boost::asio::connect(stream.next_layer(), results.begin(), results.end());
      stream.handshake(boost::asio::ssl::stream_base::client);

      /* Construct the POST request and send it */
      boost::beast::http::request<boost::beast::http::string_body> req;
      req.method(boost::beast::http::verb::post);
      req.target("/info/hw?token=????");
      req.set(boost::beast::http::field::content_type, "application/json");
      req.set(boost::beast::http::field::host, host);
      req.body() = info.dump();
      req.prepare_payload();
      boost::beast::http::write(stream, req);

      /* Get the response and ignore it */
      boost::beast::flat_buffer buffer;
      boost::beast::http::response<boost::beast::http::dynamic_body> res;
      boost::beast::http::read(stream, buffer, res);

      /* Close the connection gracefully, https://stackoverflow.com/questions/52990455/boost-asio-ssl-stream-shutdownec-always-had-error-which-is-boostasiossl */
      boost::system::error_code ec;
      stream.lowest_layer().cancel(ec);
      if(ec)
      {
        throw std::runtime_error("cancelling SSL operations failed");
      }
      stream.shutdown(ec);
      if(ec == boost::asio::error::eof || ec == boost::asio::ssl::error::stream_truncated)
      {
        ec.assign(0, ec.category());
      }
      if(ec)
      {
        continue;
      }
      return 0;
    }
    catch (const std::exception& ex)
    {
      continue;
    }
  }
  
  /* This return call should be never reached */
  return -1;
}
