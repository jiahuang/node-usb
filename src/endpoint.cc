#include "bindings.h"
#include "endpoint.h"
#include "device.h"
#include "transfer.h"

namespace NodeUsb {
	Persistent<FunctionTemplate> Endpoint::constructor_template;

	Endpoint::Endpoint(Handle<Object> _v8device, Device* _device, const libusb_endpoint_descriptor* _endpoint_descriptor, uint32_t _idx_endpoint) : ObjectWrap() {
		v8device = Persistent<Object>::New(_v8device);
		device = _device;
		descriptor = _endpoint_descriptor;
		// if bit[7] of endpoint address is set => ENDPOINT_IN (device to host), else: ENDPOINT_OUT (host to device)
		endpoint_type = (descriptor->bEndpointAddress & (1 << 7)) ? (LIBUSB_ENDPOINT_IN) : (LIBUSB_ENDPOINT_OUT);
		// bit[0] and bit[1] of bmAttributes masks transfer_type; 3 = 0000 0011
		transfer_type = (libusb_transfer_type)(3 & descriptor->bmAttributes);
		idx_endpoint = _idx_endpoint;
	}

	Endpoint::~Endpoint() {
		// TODO Close
		v8device.Dispose();
		DEBUG("Endpoint object destroyed")
	}

	void Endpoint::Initalize(Handle<Object> target) {
		DEBUG("Entering...")
		HandleScope  scope;
		Local<FunctionTemplate> t = FunctionTemplate::New(Endpoint::New);

		// Constructor
		t->InstanceTemplate()->SetInternalFieldCount(1);
		t->SetClassName(String::NewSymbol("Endpoint"));
		Endpoint::constructor_template = Persistent<FunctionTemplate>::New(t);

		Local<ObjectTemplate> instance_template = t->InstanceTemplate();

		// Constants
		// no constants at the moment
	
		// Properties
		instance_template->SetAccessor(V8STR("__endpointType"), Endpoint::EndpointTypeGetter);
		instance_template->SetAccessor(V8STR("__transferType"), Endpoint::TransferTypeGetter);
		instance_template->SetAccessor(V8STR("__maxIsoPacketSize"), Endpoint::MaxIsoPacketSizeGetter);
		instance_template->SetAccessor(V8STR("__maxPacketSize"), Endpoint::MaxPacketSizeGetter);

		// methods exposed to node.js
		NODE_SET_PROTOTYPE_METHOD(t, "getExtraData", Endpoint::GetExtraData);
		NODE_SET_PROTOTYPE_METHOD(t, "transfer", Endpoint::Transfer);

		// Make it visible in JavaScript
		target->Set(String::NewSymbol("Endpoint"), t->GetFunction());	
		DEBUG("Leave")
	}

	Handle<Value> Endpoint::New(const Arguments& args) {
		HandleScope scope;
		DEBUG("New Endpoint object created")

		if (args.Length() != 4 || !args[0]->IsObject() || !args[1]->IsUint32() || !args[2]->IsUint32()|| !args[3]->IsUint32()) {
			THROW_BAD_ARGS("Device::New argument is invalid. [object:device, uint32_t:idx_interface, uint32_t:idx_alt_setting, uint32_t:idx_endpoint]!")
		}

		// make local value reference to first parameter
		Local<Object> device = Local<Object>::Cast(args[0]);
		Device *dev = ObjectWrap::Unwrap<Device>(device);
		uint32_t idxInterface  = args[1]->Uint32Value();
		uint32_t idxAltSetting = args[2]->Uint32Value();
		uint32_t idxEndpoint   = args[3]->Uint32Value();
		
		const libusb_endpoint_descriptor *libusbEndpointDescriptor = &((dev->config_descriptor->interface[idxInterface]).altsetting[idxAltSetting]).endpoint[idxEndpoint];

		// create new Endpoint object
		Endpoint *endpoint = new Endpoint(device, dev, libusbEndpointDescriptor, idxEndpoint);
		// initalize handle

#define LIBUSB_ENDPOINT_DESCRIPTOR_STRUCT_TO_V8(name) \
		args.This()->Set(V8STR(#name), Uint32::New(endpoint->descriptor->name));
		LIBUSB_ENDPOINT_DESCRIPTOR_STRUCT_TO_V8(bLength)
		LIBUSB_ENDPOINT_DESCRIPTOR_STRUCT_TO_V8(bDescriptorType)
		LIBUSB_ENDPOINT_DESCRIPTOR_STRUCT_TO_V8(bEndpointAddress)
		LIBUSB_ENDPOINT_DESCRIPTOR_STRUCT_TO_V8(bmAttributes)
		LIBUSB_ENDPOINT_DESCRIPTOR_STRUCT_TO_V8(wMaxPacketSize)
		LIBUSB_ENDPOINT_DESCRIPTOR_STRUCT_TO_V8(bInterval)
		LIBUSB_ENDPOINT_DESCRIPTOR_STRUCT_TO_V8(bRefresh)
		LIBUSB_ENDPOINT_DESCRIPTOR_STRUCT_TO_V8(bSynchAddress)
		LIBUSB_ENDPOINT_DESCRIPTOR_STRUCT_TO_V8(extra_length)

		// wrap created Endpoint object to v8
		endpoint->Wrap(args.This());

		return args.This();
	}

	Handle<Value> Endpoint::EndpointTypeGetter(Local<String> property, const AccessorInfo &info) {
		LOCAL(Endpoint, self, info.Holder())
		
		return scope.Close(Integer::New(self->endpoint_type));
	}

	Handle<Value> Endpoint::TransferTypeGetter(Local<String> property, const AccessorInfo &info) {
		LOCAL(Endpoint, self, info.Holder())
		
		return scope.Close(Integer::New(self->transfer_type));
	}

	Handle<Value> Endpoint::MaxPacketSizeGetter(Local<String> property, const AccessorInfo &info) {
		LOCAL(Endpoint, self, info.Holder())
		int r = 0;
		
		CHECK_USB((r = libusb_get_max_packet_size(self->device->device, self->descriptor->bEndpointAddress)), scope)
		
		return scope.Close(Integer::New(r));
	}

	Handle<Value> Endpoint::MaxIsoPacketSizeGetter(Local<String> property, const AccessorInfo &info) {
		LOCAL(Endpoint, self, info.Holder())
		int r = 0;
		
		CHECK_USB((r = libusb_get_max_iso_packet_size(self->device->device, self->descriptor->bEndpointAddress)), scope)
		
		return scope.Close(Integer::New(r));
	}

	Handle<Value> Endpoint::GetExtraData(const Arguments& args) {
		LOCAL(Endpoint, self, args.This())
		 
		int m = (*self->descriptor).extra_length;
		
		Local<Array> r = Array::New(m);
		
		for (int i = 0; i < m; i++) {
		  uint32_t c = (*self->descriptor).extra[i];
		  
		  r->Set(i, Uint32::New(c));
		}
		
		return scope.Close(r);
	}

	Handle<Value> Endpoint::Transfer(const Arguments& args){
		LOCAL(Endpoint, self, args.This())
		
		CHECK_USB(self->device->openHandle(), scope);

		unsigned length, timeout;
		unsigned char *buf;
		
		//args: buffer/size, timeout, callback
		
		if (args.Length() < 3 || !args[2]->IsFunction()) {
			THROW_BAD_ARGS("Transfer missing arguments!")
		}
		
		BUF_LEN_ARG(args[0]);
		INT_ARG(timeout, args[1]);
		
		if (modus != self->endpoint_type) {
			THROW_BAD_ARGS("Transfer is used in the wrong direction (IN/OUT) for this endpoint");
		}
		
		NodeUsb::Transfer* t = Transfer::newTransfer(
			self->transfer_type,
			args.This(),
			self->descriptor->bEndpointAddress,
			buf,
			length,
			timeout,
			Handle<Function>::Cast(args[2])
		);
		
		t->submit();

		return Undefined();
	}
}
