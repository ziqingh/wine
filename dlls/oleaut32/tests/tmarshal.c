/*
 * Copyright (C) 2005-2006 Robert Shearman for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 *
 */

#include <math.h>

#define COBJMACROS
#define CONST_VTABLE

#include <windows.h>
#include <ocidl.h>
#include <stdio.h>

#include "wine/test.h"

#include "tmarshal.h"

static HRESULT (WINAPI *pVarAdd)(LPVARIANT,LPVARIANT,LPVARIANT);


#define ok_ole_success(hr, func) ok(hr == S_OK, #func " failed with error 0x%08x\n", hr)
static inline void release_iface_(unsigned int line, void *iface)
{
    ULONG ref = IUnknown_Release((IUnknown *)iface);
    ok_(__FILE__, line)(!ref, "Got outstanding refcount %d.\n", ref);
}
#define release_iface(a) release_iface_(__LINE__, a)

/* ULL suffix is not portable */
#define ULL_CONST(dw1, dw2) ((((ULONGLONG)dw1) << 32) | (ULONGLONG)dw2)

static const WCHAR test_bstr1[] = {'f','o','o',0,'b','a','r'};
static const WCHAR test_bstr2[] = {'t','e','s','t',0};
static const WCHAR test_bstr3[] = {'q','u','x',0};
static const WCHAR test_bstr4[] = {'a','b','c',0};

const MYSTRUCT MYSTRUCT_BYVAL = {0x12345678, ULL_CONST(0xdeadbeef, 0x98765432), {0,1,2,3,4,5,6,7}};
const MYSTRUCT MYSTRUCT_BYPTR = {0x91827364, ULL_CONST(0x88776655, 0x44332211), {0,1,2,3,4,5,6,7}};
const MYSTRUCT MYSTRUCT_ARRAY[5] = {
    {0x1a1b1c1d, ULL_CONST(0x1e1f1011, 0x12131415), {0,1,2,3,4,5,6,7}},
    {0x2a2b2c2d, ULL_CONST(0x2e2f2021, 0x22232425), {0,1,2,3,4,5,6,7}},
    {0x3a3b3c3d, ULL_CONST(0x3e3f3031, 0x32333435), {0,1,2,3,4,5,6,7}},
    {0x4a4b4c4d, ULL_CONST(0x4e4f4041, 0x42434445), {0,1,2,3,4,5,6,7}},
    {0x5a5b5c5d, ULL_CONST(0x5e5f5051, 0x52535455), {0,1,2,3,4,5,6,7}},
};


#define RELEASEMARSHALDATA WM_USER

struct host_object_data
{
    IStream *stream;
    IID iid;
    IUnknown *object;
    MSHLFLAGS marshal_flags;
    HANDLE marshal_event;
    IMessageFilter *filter;
};

static DWORD CALLBACK host_object_proc(LPVOID p)
{
    struct host_object_data *data = p;
    HRESULT hr;
    MSG msg;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    if (data->filter)
    {
        IMessageFilter * prev_filter = NULL;
        hr = CoRegisterMessageFilter(data->filter, &prev_filter);
        if (prev_filter) IMessageFilter_Release(prev_filter);
        ok_ole_success(hr, CoRegisterMessageFilter);
    }

    hr = CoMarshalInterface(data->stream, &data->iid, data->object, MSHCTX_INPROC, NULL, data->marshal_flags);
    ok_ole_success(hr, CoMarshalInterface);

    /* force the message queue to be created before signaling parent thread */
    PeekMessageA(&msg, NULL, WM_USER, WM_USER, PM_NOREMOVE);

    SetEvent(data->marshal_event);

    while (GetMessageA(&msg, NULL, 0, 0))
    {
        if (msg.hwnd == NULL && msg.message == RELEASEMARSHALDATA)
        {
            trace("releasing marshal data\n");
            CoReleaseMarshalData(data->stream);
            SetEvent((HANDLE)msg.lParam);
        }
        else
            DispatchMessageA(&msg);
    }

    HeapFree(GetProcessHeap(), 0, data);

    CoUninitialize();

    return hr;
}

static DWORD start_host_object2(IStream *stream, REFIID riid, IUnknown *object, MSHLFLAGS marshal_flags, IMessageFilter *filter, HANDLE *thread)
{
    DWORD tid = 0;
    HANDLE marshal_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    struct host_object_data *data = HeapAlloc(GetProcessHeap(), 0, sizeof(*data));

    data->stream = stream;
    data->iid = *riid;
    data->object = object;
    data->marshal_flags = marshal_flags;
    data->marshal_event = marshal_event;
    data->filter = filter;

    *thread = CreateThread(NULL, 0, host_object_proc, data, 0, &tid);

    /* wait for marshaling to complete before returning */
    WaitForSingleObject(marshal_event, INFINITE);
    CloseHandle(marshal_event);

    return tid;
}

static DWORD start_host_object(IStream *stream, REFIID riid, IUnknown *object, MSHLFLAGS marshal_flags, HANDLE *thread)
{
    return start_host_object2(stream, riid, object, marshal_flags, NULL, thread);
}

#if 0 /* not used */
/* asks thread to release the marshal data because it has to be done by the
 * same thread that marshaled the interface in the first place. */
static void release_host_object(DWORD tid)
{
    HANDLE event = CreateEventA(NULL, FALSE, FALSE, NULL);
    PostThreadMessageA(tid, RELEASEMARSHALDATA, 0, (LPARAM)event);
    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
}
#endif

static void end_host_object(DWORD tid, HANDLE thread)
{
    BOOL ret = PostThreadMessageA(tid, WM_QUIT, 0, 0);
    ok(ret, "PostThreadMessage failed with error %d\n", GetLastError());
    /* be careful of races - don't return until hosting thread has terminated */
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
}

static int external_connections;
static BOOL expect_last_release_closes;

static HRESULT WINAPI ExternalConnection_QueryInterface(IExternalConnection *iface, REFIID riid, void **ppv)
{
    ok(0, "unexpected call\n");
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI ExternalConnection_AddRef(IExternalConnection *iface)
{
    return 2;
}

static ULONG WINAPI ExternalConnection_Release(IExternalConnection *iface)
{
    return 1;
}

static DWORD WINAPI ExternalConnection_AddConnection(IExternalConnection *iface, DWORD extconn, DWORD reserved)
{
    trace("add connection\n");

    ok(extconn == EXTCONN_STRONG, "extconn = %d\n", extconn);
    ok(!reserved, "reserved = %x\n", reserved);
    return ++external_connections;
}

static DWORD WINAPI ExternalConnection_ReleaseConnection(IExternalConnection *iface, DWORD extconn,
        DWORD reserved, BOOL fLastReleaseCloses)
{
    trace("release connection\n");

    ok(extconn == EXTCONN_STRONG, "extconn = %d\n", extconn);
    ok(!reserved, "reserved = %x\n", reserved);

    ok(fLastReleaseCloses == expect_last_release_closes, "fLastReleaseCloses = %x, expected %x\n",
       fLastReleaseCloses, expect_last_release_closes);
    return --external_connections;
}

static const IExternalConnectionVtbl ExternalConnectionVtbl = {
    ExternalConnection_QueryInterface,
    ExternalConnection_AddRef,
    ExternalConnection_Release,
    ExternalConnection_AddConnection,
    ExternalConnection_ReleaseConnection
};

static IExternalConnection ExternalConnection = { &ExternalConnectionVtbl };

static ItestDual TestDual, TestDualDisp;

static HRESULT WINAPI TestSecondIface_QueryInterface(ITestSecondIface *iface, REFIID riid, void **ppv)
{
    return ItestDual_QueryInterface(&TestDual, riid, ppv);
}

static ULONG WINAPI TestSecondIface_AddRef(ITestSecondIface *iface)
{
    return 2;
}

static ULONG WINAPI TestSecondIface_Release(ITestSecondIface *iface)
{
    return 1;
}

static HRESULT WINAPI TestSecondIface_test(ITestSecondIface *iface)
{
    return 1;
}

static const ITestSecondIfaceVtbl TestSecondIfaceVtbl = {
    TestSecondIface_QueryInterface,
    TestSecondIface_AddRef,
    TestSecondIface_Release,
    TestSecondIface_test
};

static ITestSecondIface TestSecondIface = { &TestSecondIfaceVtbl };

static HRESULT WINAPI TestSecondDisp_QueryInterface(ITestSecondDisp *iface, REFIID riid, void **ppv)
{
    return ItestDual_QueryInterface(&TestDual, riid, ppv);
}

static ULONG WINAPI TestSecondDisp_AddRef(ITestSecondDisp *iface)
{
    return 2;
}

static ULONG WINAPI TestSecondDisp_Release(ITestSecondDisp *iface)
{
    return 1;
}

static HRESULT WINAPI TestSecondDisp_GetTypeInfoCount(ITestSecondDisp *iface, UINT *pctinfo)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI TestSecondDisp_GetTypeInfo(ITestSecondDisp *iface, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI TestSecondDisp_GetIDsOfNames(ITestSecondDisp *iface, REFIID riid, LPOLESTR *rgszNames,
        UINT cNames, LCID lcid, DISPID *rgDispId)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI TestSecondDisp_Invoke(ITestSecondDisp *iface, DISPID dispIdMember, REFIID riid, LCID lcid,
        WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo,
        UINT *puArgErr)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI TestSecondDisp_test(ITestSecondDisp *iface)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static ITestSecondDispVtbl TestSecondDispVtbl = {
    TestSecondDisp_QueryInterface,
    TestSecondDisp_AddRef,
    TestSecondDisp_Release,
    TestSecondDisp_GetTypeInfoCount,
    TestSecondDisp_GetTypeInfo,
    TestSecondDisp_GetIDsOfNames,
    TestSecondDisp_Invoke,
    TestSecondDisp_test
};

static ITestSecondDisp TestSecondDisp = { &TestSecondDispVtbl };

static HRESULT WINAPI TestDual_QueryInterface(ItestDual *iface, REFIID riid, void **ppvObject)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch)) {
        *ppvObject = &TestDualDisp;
        return S_OK;
    }else if(IsEqualGUID(riid, &IID_ItestDual)) {
        *ppvObject = &TestDual;
        return S_OK;
    }else if(IsEqualGUID(riid, &IID_ITestSecondIface)) {
        *ppvObject = &TestSecondIface;
        return S_OK;
    }else if(IsEqualGUID(riid, &IID_ITestSecondDisp)) {
        *ppvObject = &TestSecondDisp;
        return S_OK;
    }else if (IsEqualGUID(riid, &IID_IExternalConnection)) {
        trace("QI external connection\n");
        *ppvObject = &ExternalConnection;
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI TestDual_AddRef(ItestDual *iface)
{
    return 2;
}

static ULONG WINAPI TestDual_Release(ItestDual *iface)
{
    return 1;
}

static HRESULT WINAPI TestDual_GetTypeInfoCount(ItestDual *iface, UINT *pctinfo)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI TestDual_GetTypeInfo(ItestDual *iface, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI TestDual_GetIDsOfNames(ItestDual *iface, REFIID riid, LPOLESTR *rgszNames,
        UINT cNames, LCID lcid, DISPID *rgDispId)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI TestDual_Invoke(ItestDual *iface, DISPID dispIdMember, REFIID riid, LCID lcid,
        WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo,
        UINT *puArgErr)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static ItestDualVtbl TestDualVtbl = {
    TestDual_QueryInterface,
    TestDual_AddRef,
    TestDual_Release,
    TestDual_GetTypeInfoCount,
    TestDual_GetTypeInfo,
    TestDual_GetIDsOfNames,
    TestDual_Invoke
};

static ItestDual TestDual = { &TestDualVtbl };
static ItestDual TestDualDisp = { &TestDualVtbl };

struct disp_obj
{
    ISomethingFromDispatch ISomethingFromDispatch_iface;
    LONG ref;
};

static inline struct disp_obj *impl_from_ISomethingFromDispatch(ISomethingFromDispatch *iface)
{
    return CONTAINING_RECORD(iface, struct disp_obj, ISomethingFromDispatch_iface);
}

static HRESULT WINAPI disp_obj_QueryInterface(ISomethingFromDispatch *iface, REFIID iid, void **out)
{
    if (IsEqualGUID(iid, &IID_IUnknown) || IsEqualGUID(iid, &IID_IDispatch)
            || IsEqualGUID(iid, &IID_ISomethingFromDispatch))
    {
        *out = iface;
        ISomethingFromDispatch_AddRef(iface);
        return S_OK;
    }

    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI disp_obj_AddRef(ISomethingFromDispatch *iface)
{
    struct disp_obj *obj = impl_from_ISomethingFromDispatch(iface);
    return ++obj->ref;
}

static ULONG WINAPI disp_obj_Release(ISomethingFromDispatch *iface)
{
    struct disp_obj *obj = impl_from_ISomethingFromDispatch(iface);
    LONG ref = --obj->ref;
    if (!ref)
        CoTaskMemFree(obj);
    return ref;
}

static HRESULT WINAPI disp_obj_GetTypeInfoCount(ISomethingFromDispatch *iface, UINT *count)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI disp_obj_GetTypeInfo(ISomethingFromDispatch *iface,
        UINT index, LCID lcid, ITypeInfo **typeinfo)
{
    ok(index == 0xdeadbeef, "Got unexpected index %#x.\n", index);
    return 0xbeefdead;
}

static HRESULT WINAPI disp_obj_GetIDsOfNames(ISomethingFromDispatch *iface,
        REFIID iid, LPOLESTR *names, UINT count, LCID lcid, DISPID *ids)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI disp_obj_Invoke(ISomethingFromDispatch *iface, DISPID id, REFIID iid, LCID lcid,
        WORD flags, DISPPARAMS *dispparams, VARIANT *result, EXCEPINFO *excepinfo, UINT *errarg)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI disp_obj_anotherfn(ISomethingFromDispatch *iface)
{
    return 0x01234567;
}

static const ISomethingFromDispatchVtbl disp_obj_vtbl =
{
    disp_obj_QueryInterface,
    disp_obj_AddRef,
    disp_obj_Release,
    disp_obj_GetTypeInfoCount,
    disp_obj_GetTypeInfo,
    disp_obj_GetIDsOfNames,
    disp_obj_Invoke,
    disp_obj_anotherfn,
};

static ISomethingFromDispatch *create_disp_obj(void)
{
    struct disp_obj *obj = CoTaskMemAlloc(sizeof(*obj));
    obj->ISomethingFromDispatch_iface.lpVtbl = &disp_obj_vtbl;
    obj->ref = 1;
    return &obj->ISomethingFromDispatch_iface;
}

static int testmode;

typedef struct Widget
{
    IWidget IWidget_iface;
    LONG refs;
    IUnknown *pDispatchUnknown;
} Widget;

static inline Widget *impl_from_IWidget(IWidget *iface)
{
    return CONTAINING_RECORD(iface, Widget, IWidget_iface);
}

static HRESULT WINAPI Widget_QueryInterface(
    IWidget *iface,
    /* [in] */ REFIID riid,
    /* [iid_is][out] */ void __RPC_FAR *__RPC_FAR *ppvObject)
{
    if (IsEqualIID(riid, &IID_IWidget) || IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch))
    {
        IWidget_AddRef(iface);
        *ppvObject = iface;
        return S_OK;
    }
    else
    {
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }
}

static ULONG WINAPI Widget_AddRef(
    IWidget *iface)
{
    Widget *This = impl_from_IWidget(iface);

    return InterlockedIncrement(&This->refs);
}

static ULONG WINAPI Widget_Release(
    IWidget *iface)
{
    Widget *This = impl_from_IWidget(iface);
    ULONG refs = InterlockedDecrement(&This->refs);
    if (!refs)
    {
        IUnknown_Release(This->pDispatchUnknown);
        memset(This, 0xcc, sizeof(*This));
        HeapFree(GetProcessHeap(), 0, This);
        trace("Widget destroyed!\n");
    }

    return refs;
}

static HRESULT WINAPI Widget_GetTypeInfoCount(
    IWidget *iface,
    /* [out] */ UINT __RPC_FAR *pctinfo)
{
    Widget *This = impl_from_IWidget(iface);
    IDispatch *pDispatch;
    HRESULT hr = IUnknown_QueryInterface(This->pDispatchUnknown, &IID_IDispatch, (void **)&pDispatch);
    if (SUCCEEDED(hr))
    {
        hr = IDispatch_GetTypeInfoCount(pDispatch, pctinfo);
        IDispatch_Release(pDispatch);
    }
    return hr;
}

static HRESULT WINAPI Widget_GetTypeInfo(
    IWidget __RPC_FAR * iface,
    /* [in] */ UINT iTInfo,
    /* [in] */ LCID lcid,
    /* [out] */ ITypeInfo __RPC_FAR *__RPC_FAR *ppTInfo)
{
    Widget *This = impl_from_IWidget(iface);
    IDispatch *pDispatch;
    HRESULT hr = IUnknown_QueryInterface(This->pDispatchUnknown, &IID_IDispatch, (void **)&pDispatch);
    if (SUCCEEDED(hr))
    {
        hr = IDispatch_GetTypeInfo(pDispatch, iTInfo, lcid, ppTInfo);
        IDispatch_Release(pDispatch);
    }
    return hr;
}

static HRESULT WINAPI Widget_GetIDsOfNames(
    IWidget __RPC_FAR * iface,
    /* [in] */ REFIID riid,
    /* [size_is][in] */ LPOLESTR __RPC_FAR *rgszNames,
    /* [in] */ UINT cNames,
    /* [in] */ LCID lcid,
    /* [size_is][out] */ DISPID __RPC_FAR *rgDispId)
{
    Widget *This = impl_from_IWidget(iface);
    IDispatch *pDispatch;
    HRESULT hr = IUnknown_QueryInterface(This->pDispatchUnknown, &IID_IDispatch, (void **)&pDispatch);
    if (SUCCEEDED(hr))
    {
        hr = IDispatch_GetIDsOfNames(pDispatch, riid, rgszNames, cNames, lcid, rgDispId);
        IDispatch_Release(pDispatch);
    }
    return hr;
}

static HRESULT WINAPI Widget_Invoke(
    IWidget __RPC_FAR * iface,
    /* [in] */ DISPID dispIdMember,
    /* [in] */ REFIID riid,
    /* [in] */ LCID lcid,
    /* [in] */ WORD wFlags,
    /* [out][in] */ DISPPARAMS __RPC_FAR *pDispParams,
    /* [out] */ VARIANT __RPC_FAR *pVarResult,
    /* [out] */ EXCEPINFO __RPC_FAR *pExcepInfo,
    /* [out] */ UINT __RPC_FAR *puArgErr)
{
    Widget *This = impl_from_IWidget(iface);
    IDispatch *pDispatch;
    HRESULT hr = IUnknown_QueryInterface(This->pDispatchUnknown, &IID_IDispatch, (void **)&pDispatch);
    if (SUCCEEDED(hr))
    {
        hr = IDispatch_Invoke(pDispatch, dispIdMember, riid, lcid, wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);
        IDispatch_Release(pDispatch);
    }
    return hr;
}

static HRESULT WINAPI Widget_put_Name(
    IWidget __RPC_FAR * iface,
    /* [in] */ BSTR name)
{
    trace("put_Name(%s)\n", wine_dbgstr_w(name));
    return S_OK;
}

static HRESULT WINAPI Widget_get_Name(
    IWidget __RPC_FAR * iface,
    /* [out] */ BSTR __RPC_FAR *name)
{
    static const WCHAR szCat[] = { 'C','a','t',0 };
    trace("get_Name()\n");
    *name = SysAllocString(szCat);
    return S_OK;
}

static HRESULT WINAPI Widget_DoSomething(
    IWidget __RPC_FAR * iface,
    /* [in] */ double number,
    /* [out] */ BSTR *str1,
    /* [defaultvalue][in] */ BSTR str2,
    /* [optional][in] */ VARIANT __RPC_FAR *opt)
{
    static const WCHAR szString[] = { 'S','t','r','i','n','g',0 };
    trace("DoSomething()\n");

    ok(number == 3.141, "number(%f) != 3.141\n", number);
    ok(*str2 == '\0', "str2(%s) != \"\"\n", wine_dbgstr_w(str2));
    ok(V_VT(opt) == VT_ERROR, "V_VT(opt) should be VT_ERROR instead of 0x%x\n", V_VT(opt));
    ok(V_ERROR(opt) == DISP_E_PARAMNOTFOUND, "V_ERROR(opt) should be DISP_E_PARAMNOTFOUND instead of 0x%08x\n", V_ERROR(opt));
    *str1 = SysAllocString(szString);

    return S_FALSE;
}

static HRESULT WINAPI Widget_get_State(
    IWidget __RPC_FAR * iface,
    /* [retval][out] */ STATE __RPC_FAR *state)
{
    trace("get_State() = STATE_WIDGETIFIED\n");
    *state = STATE_WIDGETIFIED;
    return S_OK;
}

static HRESULT WINAPI Widget_put_State(
    IWidget __RPC_FAR * iface,
    /* [in] */ STATE state)
{
    trace("put_State(%d)\n", state);
    return S_OK;
}

static HRESULT WINAPI Widget_Map(
    IWidget * iface,
    BSTR bstrId,
    BSTR *sValue)
{
    trace("Map(%s, %p)\n", wine_dbgstr_w(bstrId), sValue);
    *sValue = SysAllocString(bstrId);
    return S_OK;
}

static HRESULT WINAPI Widget_SetOleColor(
    IWidget * iface,
    OLE_COLOR val)
{
    trace("SetOleColor(0x%x)\n", val);
    return S_OK;
}

static HRESULT WINAPI Widget_GetOleColor(
    IWidget * iface,
    OLE_COLOR *pVal)
{
    trace("GetOleColor() = 0x8000000f\n");
    *pVal = 0x8000000f;
    return S_FALSE;
}

static HRESULT WINAPI Widget_Clone(
    IWidget *iface,
    IWidget **ppVal)
{
    trace("Clone()\n");
    return Widget_QueryInterface(iface, &IID_IWidget, (void **)ppVal);
}

static HRESULT WINAPI Widget_CloneDispatch(
    IWidget *iface,
    IDispatch **ppVal)
{
    trace("CloneDispatch()\n");
    return Widget_QueryInterface(iface, &IID_IWidget, (void **)ppVal);
}

static HRESULT WINAPI Widget_CloneCoclass(
    IWidget *iface,
    ApplicationObject2 **ppVal)
{
    trace("CloneCoclass()\n");
    return Widget_QueryInterface(iface, &IID_IWidget, (void **)ppVal);
}

static HRESULT WINAPI Widget_Value(
    IWidget __RPC_FAR * iface,
    VARIANT *value,
    VARIANT *retval)
{
    trace("Value(%p, %p)\n", value, retval);
    ok(V_VT(value) == VT_I2, "V_VT(value) was %d instead of VT_I2\n", V_VT(value));
    ok(V_I2(value) == 1, "V_I2(value) was %d instead of 1\n", V_I2(value));
    V_VT(retval) = VT_I2;
    V_I2(retval) = 1234;
    return S_OK;
}

static HRESULT WINAPI Widget_Array(
    IWidget * iface,
    SAFEARRAY * values)
{
    trace("Array(%p)\n", values);
    return S_OK;
}

static HRESULT WINAPI Widget_VariantArrayPtr(
    IWidget * iface,
    SAFEARRAY ** values)
{
    trace("VariantArrayPtr(%p)\n", values);
    return S_OK;
}

static HRESULT WINAPI Widget_VariantCArray(
    IWidget * iface,
    ULONG count,
    VARIANT values[])
{
    ULONG i;

    trace("VariantCArray(%u,%p)\n", count, values);

    ok(count == 2, "count is %d\n", count);
    for (i = 0; i < count; i++)
        ok(V_VT(&values[i]) == VT_I4, "values[%d] is not VT_I4\n", i);

    if (pVarAdd)
    {
        VARIANT inc, res;
        HRESULT hr;

        V_VT(&inc) = VT_I4;
        V_I4(&inc) = 1;
        for (i = 0; i < count; i++) {
            VariantInit(&res);
            hr = pVarAdd(&values[i], &inc, &res);
            if (FAILED(hr)) {
                ok(0, "VarAdd failed at %u with error 0x%x\n", i, hr);
                return hr;
            }
            hr = VariantCopy(&values[i], &res);
            if (FAILED(hr)) {
                ok(0, "VariantCopy failed at %u with error 0x%x\n", i, hr);
                return hr;
            }
        }
    }
    else
        win_skip("VarAdd is not available\n");

    return S_OK;
}

static HRESULT WINAPI Widget_VarArg(
    IWidget * iface,
    int numexpect,
    SAFEARRAY * values)
{
    LONG lbound, ubound, i;
    VARIANT * data;
    HRESULT hr;

    trace("VarArg(%p)\n", values);

    hr = SafeArrayGetLBound(values, 1, &lbound);
    ok(hr == S_OK, "SafeArrayGetLBound failed with %x\n", hr);
    ok(lbound == 0, "SafeArrayGetLBound returned %d\n", lbound);

    hr = SafeArrayGetUBound(values, 1, &ubound);
    ok(hr == S_OK, "SafeArrayGetUBound failed with %x\n", hr);
    ok(ubound == numexpect-1, "SafeArrayGetUBound returned %d, but expected %d\n", ubound, numexpect-1);

    hr = SafeArrayAccessData(values, (LPVOID)&data);
    ok(hr == S_OK, "SafeArrayAccessData failed with %x\n", hr);

    for (i=0; i<=ubound-lbound; i++)
    {
        ok(V_VT(&data[i]) == VT_I4, "V_VT(&data[%d]) was %d\n", i, V_VT(&data[i]));
        ok(V_I4(&data[i]) == i, "V_I4(&data[%d]) was %d\n", i, V_I4(&data[i]));
    }

    hr = SafeArrayUnaccessData(values);
    ok(hr == S_OK, "SafeArrayUnaccessData failed with %x\n", hr);

    return S_OK;
}


static BOOL mystruct_uint_ordered(MYSTRUCT *mystruct)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(mystruct->uarr); i++)
        if (mystruct->uarr[i] != i)
            return FALSE;
    return TRUE;
}

static HRESULT WINAPI Widget_StructArgs(
    IWidget * iface,
    MYSTRUCT byval,
    MYSTRUCT *byptr,
    MYSTRUCT arr[5])
{
    int i, diff = 0;
    ok(byval.field1 == MYSTRUCT_BYVAL.field1 &&
       byval.field2 == MYSTRUCT_BYVAL.field2 &&
       mystruct_uint_ordered(&byval),
       "Struct parameter passed by value corrupted\n");
    ok(byptr->field1 == MYSTRUCT_BYPTR.field1 &&
       byptr->field2 == MYSTRUCT_BYPTR.field2 &&
       mystruct_uint_ordered(byptr),
       "Struct parameter passed by pointer corrupted\n");
    for (i = 0; i < 5; i++)
        if (arr[i].field1 != MYSTRUCT_ARRAY[i].field1 ||
            arr[i].field2 != MYSTRUCT_ARRAY[i].field2 ||
            ! mystruct_uint_ordered(&arr[i]))
            diff++;
    ok(diff == 0, "Array of structs corrupted\n");
    return S_OK;
}


static HRESULT WINAPI Widget_Error(
    IWidget __RPC_FAR * iface)
{
    trace("Error()\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI Widget_CloneInterface(
    IWidget __RPC_FAR * iface,
    ISomethingFromDispatch **ppVal)
{
    trace("CloneInterface()\n");
    *ppVal = 0;
    return S_OK;
}

static HRESULT WINAPI Widget_put_prop_with_lcid(
    IWidget* iface, LONG lcid, INT i)
{
    trace("put_prop_with_lcid(%08x, %x)\n", lcid, i);
    ok(lcid == MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), "got lcid %08x\n", lcid);
    ok(i == 0xcafe, "got %08x\n", i);
    return S_OK;
}

static HRESULT WINAPI Widget_get_prop_with_lcid(
    IWidget* iface, LONG lcid, INT *i)
{
    trace("get_prop_with_lcid(%08x, %p)\n", lcid, i);
    ok(lcid == MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US), "got lcid %08x\n", lcid);
    *i = lcid;
    return S_OK;
}

static HRESULT WINAPI Widget_get_prop_int(
    IWidget* iface, INT *i)
{
    trace("get_prop_int(%p)\n", i);
    *i = -13;
    return S_OK;
}

static HRESULT WINAPI Widget_get_prop_uint(
    IWidget* iface, UINT *i)
{
    trace("get_prop_uint(%p)\n", i);
    *i = 42;
    return S_OK;
}

static HRESULT WINAPI Widget_ByRefUInt(
    IWidget* iface, UINT *i)
{
    *i = 42;
    return S_OK;
}

static HRESULT WINAPI Widget_put_prop_opt_arg(
    IWidget* iface, INT opt, INT i)
{
    trace("put_prop_opt_arg(%08x, %08x)\n", opt, i);
    todo_wine ok(opt == 0, "got opt=%08x\n", opt);
    ok(i == 0xcafe, "got i=%08x\n", i);
    return S_OK;
}

static HRESULT WINAPI Widget_put_prop_req_arg(
    IWidget* iface, INT req, INT i)
{
    trace("put_prop_req_arg(%08x, %08x)\n", req, i);
    ok(req == 0x5678, "got req=%08x\n", req);
    ok(i == 0x1234, "got i=%08x\n", i);
    return S_OK;
}

static HRESULT WINAPI Widget_pos_restrict(IWidget* iface, INT *i)
{
    trace("restrict\n");
    *i = DISPID_TM_RESTRICTED;
    return S_OK;
}

static HRESULT WINAPI Widget_neg_restrict(IWidget* iface, INT *i)
{
    trace("neg_restrict\n");
    *i = DISPID_TM_NEG_RESTRICTED;
    return S_OK;
}

static HRESULT WINAPI Widget_VarArg_Run(
    IWidget *iface, BSTR name, SAFEARRAY *params, VARIANT *result)
{
    static const WCHAR catW[] = { 'C','a','t',0 };
    static const WCHAR supermanW[] = { 'S','u','p','e','r','m','a','n',0 };
    LONG bound;
    VARIANT *var;
    BSTR bstr;
    HRESULT hr;

    trace("VarArg_Run(%p,%p,%p)\n", name, params, result);

    ok(!lstrcmpW(name, catW), "got %s\n", wine_dbgstr_w(name));

    hr = SafeArrayGetLBound(params, 1, &bound);
    ok(hr == S_OK, "SafeArrayGetLBound error %#x\n", hr);
    ok(bound == 0, "expected 0, got %d\n", bound);

    hr = SafeArrayGetUBound(params, 1, &bound);
    ok(hr == S_OK, "SafeArrayGetUBound error %#x\n", hr);
    ok(bound == 0, "expected 0, got %d\n", bound);

    hr = SafeArrayAccessData(params, (void **)&var);
    ok(hr == S_OK, "SafeArrayAccessData failed with %x\n", hr);

    ok(V_VT(&var[0]) == VT_BSTR, "expected VT_BSTR, got %d\n", V_VT(&var[0]));
    bstr = V_BSTR(&var[0]);
    ok(!lstrcmpW(bstr, supermanW), "got %s\n", wine_dbgstr_w(bstr));

    hr = SafeArrayUnaccessData(params);
    ok(hr == S_OK, "SafeArrayUnaccessData error %#x\n", hr);

    return S_OK;
}

static HRESULT WINAPI Widget_VarArg_Ref_Run(
    IWidget *iface, BSTR name, SAFEARRAY **params, VARIANT *result)
{
    static const WCHAR catW[] = { 'C','a','t',0 };
    static const WCHAR supermanW[] = { 'S','u','p','e','r','m','a','n',0 };
    LONG bound;
    VARIANT *var;
    BSTR bstr;
    HRESULT hr;

    trace("VarArg_Ref_Run(%p,%p,%p)\n", name, params, result);

    ok(!lstrcmpW(name, catW), "got %s\n", wine_dbgstr_w(name));

    hr = SafeArrayGetLBound(*params, 1, &bound);
    ok(hr == S_OK, "SafeArrayGetLBound error %#x\n", hr);
    ok(bound == 0, "expected 0, got %d\n", bound);

    hr = SafeArrayGetUBound(*params, 1, &bound);
    ok(hr == S_OK, "SafeArrayGetUBound error %#x\n", hr);
    ok(bound == 0, "expected 0, got %d\n", bound);

    hr = SafeArrayAccessData(*params, (void **)&var);
    ok(hr == S_OK, "SafeArrayAccessData error %#x\n", hr);

    ok(V_VT(&var[0]) == VT_BSTR, "expected VT_BSTR, got %d\n", V_VT(&var[0]));
    bstr = V_BSTR(&var[0]);
    ok(!lstrcmpW(bstr, supermanW), "got %s\n", wine_dbgstr_w(bstr));

    hr = SafeArrayUnaccessData(*params);
    ok(hr == S_OK, "SafeArrayUnaccessData error %#x\n", hr);

    return S_OK;
}

static HRESULT WINAPI Widget_Coclass(
    IWidget *iface, ApplicationObject2 *param)
{
    trace("Coclass(%p)\n", param);
    ok(param == (ApplicationObject2 *)iface, "expected param == %p, got %p\n", iface, param);
    return S_OK;
}

static HRESULT WINAPI Widget_basetypes_in(IWidget *iface, signed char c, short s, int i, hyper h,
        unsigned char uc, unsigned short us, unsigned int ui, MIDL_uhyper uh,
        float f, double d, STATE st)
{
    ok(c == 5, "Got char %d.\n", c);
    ok(s == -123, "Got short %d.\n", s);
    ok(i == -100000, "Got int %d.\n", i);
    ok(h == (LONGLONG)-100000 * 1000000, "Got hyper %s.\n", wine_dbgstr_longlong(h));
    ok(uc == 0, "Got unsigned char %u.\n", uc);
    ok(us == 456, "Got unsigned short %u.\n", us);
    ok(ui == 0xdeadbeef, "Got unsigned int %i.\n", ui);
    ok(uh == (ULONGLONG)1234567890 * 9876543210, "Got unsigned hyper %s.\n", wine_dbgstr_longlong(uh));
    ok(f == (float)M_PI, "Got float %f.\n", f);
    ok(d == M_E, "Got double %f.\n", d);
    ok(st == STATE_WIDGETIFIED, "Got state %u.\n", st);

    return S_OK;
}

static HRESULT WINAPI Widget_basetypes_out(IWidget *iface, signed char *c, short *s, int *i, hyper *h,
        unsigned char *uc, unsigned short *us, unsigned int *ui, MIDL_uhyper *uh,
        float *f, double *d, STATE *st)
{
    *c = 10;
    *s = -321;
    *i = -200000;
    *h = (LONGLONG)-200000 * 1000000;
    *uc = 254;
    *us = 256;
    *ui = 0xf00dfade;
    *uh = (((ULONGLONG)0xabcdef01) << 32) | (ULONGLONG)0x23456789;
    *f = M_LN2;
    *d = M_LN10;
    *st = STATE_UNWIDGETIFIED;

    return S_OK;
}

static HRESULT WINAPI Widget_int_ptr(IWidget *iface, int *in, int *out, int *in_out)
{
    ok(*in == 123, "Got [in] %d.\n", *in);
    if (testmode == 0)  /* Invoke() */
        ok(*out == 456, "Got [out] %d.\n", *out);
    else if (testmode == 1)
        ok(!*out, "Got [out] %d.\n", *out);
    ok(*in_out == 789, "Got [in, out] %d.\n", *in_out);

    *in = 987;
    *out = 654;
    *in_out = 321;

    return S_OK;
}

static HRESULT WINAPI Widget_int_ptr_ptr(IWidget *iface, int **in, int **out, int **in_out)
{
todo_wine_if(testmode == 2)
    ok(!*out, "Got [out] %p.\n", *out);
    if (testmode == 0)
    {
        ok(!*in, "Got [in] %p.\n", *in);
        ok(!*in_out, "Got [in, out] %p.\n", *in_out);
    }
    else if (testmode == 1)
    {
        ok(!*in, "Got [in] %p.\n", *in);
        ok(!*in_out, "Got [in, out] %p.\n", *in_out);

        *out = CoTaskMemAlloc(sizeof(int));
        **out = 654;
        *in_out = CoTaskMemAlloc(sizeof(int));
        **in_out = 321;
    }
    else if (testmode == 2)
    {
        ok(**in == 123, "Got [in] %d.\n", **in);
        ok(**in_out == 789, "Got [in, out] %d.\n", **in_out);

        *out = CoTaskMemAlloc(sizeof(int));
        **out = 654;
        **in_out = 321;
    }
    else if (testmode == 3)
    {
        ok(**in_out == 789, "Got [in, out] %d.\n", **in_out);
        *in_out = NULL;
    }

    return S_OK;
}

/* Call methods to check that we have valid proxies to each interface. */
static void check_iface_marshal(IUnknown *unk, IDispatch *disp, ISomethingFromDispatch *sfd)
{
    ISomethingFromDispatch *sfd2;
    ITypeInfo *typeinfo;
    HRESULT hr;

    hr = IUnknown_QueryInterface(unk, &IID_ISomethingFromDispatch, (void **)&sfd2);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ISomethingFromDispatch_Release(sfd2);

    hr = IDispatch_GetTypeInfo(disp, 0xdeadbeef, 0, &typeinfo);
    ok(hr == 0xbeefdead, "Got hr %#x.\n", hr);

    hr = ISomethingFromDispatch_anotherfn(sfd);
    ok(hr == 0x01234567, "Got hr %#x.\n", hr);
}

static HRESULT WINAPI Widget_iface_in(IWidget *iface, IUnknown *unk, IDispatch *disp, ISomethingFromDispatch *sfd)
{
    if (testmode == 0)
        check_iface_marshal(unk, disp, sfd);
    else if (testmode == 1)
    {
        ok(!unk, "Got iface %p.\n", unk);
        ok(!disp, "Got iface %p.\n", disp);
        ok(!sfd, "Got iface %p.\n", sfd);
    }
    return S_OK;
}

static HRESULT WINAPI Widget_iface_out(IWidget *iface, IUnknown **unk, IDispatch **disp, ISomethingFromDispatch **sfd)
{
todo_wine
    ok(!*unk, "Got iface %p.\n", *unk);
    ok(!*disp, "Got iface %p.\n", *disp);
    ok(!*sfd, "Got iface %p.\n", *sfd);

    if (testmode == 0)
    {
        *unk = (IUnknown *)create_disp_obj();
        *disp = (IDispatch *)create_disp_obj();
        *sfd = create_disp_obj();
    }
    return S_OK;
}

static HRESULT WINAPI Widget_iface_ptr(IWidget *iface, ISomethingFromDispatch **in,
        ISomethingFromDispatch **out, ISomethingFromDispatch **in_out)
{
    HRESULT hr;

    ok(!*out, "Got [out] %p.\n", *out);
    if (testmode == 0 || testmode == 1)
    {
        hr = ISomethingFromDispatch_anotherfn(*in);
        ok(hr == 0x01234567, "Got hr %#x.\n", hr);
        hr = ISomethingFromDispatch_anotherfn(*in_out);
        ok(hr == 0x01234567, "Got hr %#x.\n", hr);
    }

    if (testmode == 1)
    {
        *out = create_disp_obj();
        ISomethingFromDispatch_Release(*in_out);
        *in_out = create_disp_obj();
    }
    else if (testmode == 2)
    {
        ok(!*in, "Got [in] %p.\n", *in);
        ok(!*in_out, "Got [in, out] %p.\n", *in_out);
        *in_out = create_disp_obj();
    }
    else if (testmode == 3)
    {
        hr = ISomethingFromDispatch_anotherfn(*in_out);
        ok(hr == 0x01234567, "Got hr %#x.\n", hr);
        ISomethingFromDispatch_Release(*in_out);
        *in_out = NULL;
    }

    return S_OK;
}

static HRESULT WINAPI Widget_bstr(IWidget *iface, BSTR in, BSTR *out, BSTR *in_ptr, BSTR *in_out)
{
    UINT len;

    if (testmode == 0)
    {
        len = SysStringByteLen(in);
        ok(len == sizeof(test_bstr1), "Got wrong length %u.\n", len);
        ok(!memcmp(in, test_bstr1, len), "Got string %s.\n", wine_dbgstr_wn(in, len / sizeof(WCHAR)));
todo_wine_if(*out)
        ok(!*out, "Got unexpected output %p.\n", *out);
        len = SysStringLen(*in_ptr);
        ok(len == lstrlenW(test_bstr2), "Got wrong length %u.\n", len);
        ok(!memcmp(*in_ptr, test_bstr2, len), "Got string %s.\n", wine_dbgstr_w(*in_ptr));
        len = SysStringLen(*in_out);
        ok(len == lstrlenW(test_bstr3), "Got wrong length %u.\n", len);
        ok(!memcmp(*in_out, test_bstr3, len), "Got string %s.\n", wine_dbgstr_w(*in_out));

        *out = SysAllocString(test_bstr4);
        in[1] = (*in_ptr)[1] = (*in_out)[1] = 'X';
    }
    else if (testmode == 1)
    {
        ok(!in, "Got string %s.\n", wine_dbgstr_w(in));
        ok(!*out, "Got string %s.\n", wine_dbgstr_w(*out));
        ok(!*in_ptr, "Got string %s.\n", wine_dbgstr_w(*in_ptr));
        ok(!*in_out, "Got string %s.\n", wine_dbgstr_w(*in_out));
    }
    return S_OK;
}

static HRESULT WINAPI Widget_variant(IWidget *iface, VARIANT in, VARIANT *out, VARIANT *in_ptr, VARIANT *in_out)
{
    ok(V_VT(&in) == VT_CY, "Got wrong type %#x.\n", V_VT(&in));
    ok(V_CY(&in).Hi == 0xdababe && V_CY(&in).Lo == 0xdeadbeef,
            "Got wrong value %s.\n", wine_dbgstr_longlong(V_CY(&in).int64));
    if (testmode == 0)
    {
        ok(V_VT(out) == VT_I4, "Got wrong type %u.\n", V_VT(out));
        ok(V_I4(out) == 1, "Got wrong value %d.\n", V_I4(out));
    }
    else
        ok(V_VT(out) == VT_EMPTY, "Got wrong type %u.\n", V_VT(out));
    ok(V_VT(in_ptr) == VT_I4, "Got wrong type %u.\n", V_VT(in_ptr));
    ok(V_I4(in_ptr) == -1, "Got wrong value %d.\n", V_I4(in_ptr));
    ok(V_VT(in_out) == VT_BSTR, "Got wrong type %u.\n", V_VT(in_out));
    ok(!lstrcmpW(V_BSTR(in_out), test_bstr2), "Got wrong value %s.\n",
            wine_dbgstr_w(V_BSTR(in_out)));

    V_VT(&in) = VT_I4;
    V_I4(&in) = 2;
    V_VT(out) = VT_UI1;
    V_UI1(out) = 3;
    V_VT(in_ptr) = VT_I2;
    V_I2(in_ptr) = 4;
    VariantClear(in_out);
    V_VT(in_out) = VT_I1;
    V_I1(in_out) = 5;
    return S_OK;
}

static const struct IWidgetVtbl Widget_VTable =
{
    Widget_QueryInterface,
    Widget_AddRef,
    Widget_Release,
    Widget_GetTypeInfoCount,
    Widget_GetTypeInfo,
    Widget_GetIDsOfNames,
    Widget_Invoke,
    Widget_put_Name,
    Widget_get_Name,
    Widget_DoSomething,
    Widget_get_State,
    Widget_put_State,
    Widget_Map,
    Widget_SetOleColor,
    Widget_GetOleColor,
    Widget_Clone,
    Widget_CloneDispatch,
    Widget_CloneCoclass,
    Widget_Value,
    Widget_Array,
    Widget_VariantArrayPtr,
    Widget_VariantCArray,
    Widget_VarArg,
    Widget_StructArgs,
    Widget_Error,
    Widget_CloneInterface,
    Widget_put_prop_with_lcid,
    Widget_get_prop_with_lcid,
    Widget_get_prop_int,
    Widget_get_prop_uint,
    Widget_ByRefUInt,
    Widget_put_prop_opt_arg,
    Widget_put_prop_req_arg,
    Widget_pos_restrict,
    Widget_neg_restrict,
    Widget_VarArg_Run,
    Widget_VarArg_Ref_Run,
    Widget_Coclass,
    Widget_basetypes_in,
    Widget_basetypes_out,
    Widget_int_ptr,
    Widget_int_ptr_ptr,
    Widget_iface_in,
    Widget_iface_out,
    Widget_iface_ptr,
    Widget_bstr,
    Widget_variant,
};

static HRESULT WINAPI StaticWidget_QueryInterface(IStaticWidget *iface, REFIID riid, void **ppvObject)
{
    if (IsEqualIID(riid, &IID_IStaticWidget) || IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDispatch))
    {
        IStaticWidget_AddRef(iface);
        *ppvObject = iface;
        return S_OK;
    }

    *ppvObject = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI StaticWidget_AddRef(IStaticWidget *iface)
{
    return 2;
}

static ULONG WINAPI StaticWidget_Release(IStaticWidget *iface)
{
    return 1;
}

static HRESULT WINAPI StaticWidget_GetTypeInfoCount(IStaticWidget *iface, UINT *pctinfo)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI StaticWidget_GetTypeInfo(IStaticWidget *iface, UINT iTInfo, LCID lcid,
        ITypeInfo **ppTInfo)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI StaticWidget_GetIDsOfNames(IStaticWidget *iface, REFIID riid, LPOLESTR *rgszNames,
        UINT cNames, LCID lcid, DISPID *rgDispId)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI StaticWidget_Invoke(IStaticWidget *iface, DISPID dispIdMember, REFIID riid,
        LCID lcid, WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo,
         UINT *puArgErr)
{
    ok(0, "unexpected call\n");
    return E_NOTIMPL;
}

static HRESULT WINAPI StaticWidget_TestDual(IStaticWidget *iface, ItestDual *p)
{
    trace("TestDual()\n");
    ok(p == &TestDual, "wrong ItestDual\n");
    return S_OK;
}

static HRESULT WINAPI StaticWidget_TestSecondIface(IStaticWidget *iface, ITestSecondIface *p)
{
    trace("TestSecondIface()\n");
    ok(p == &TestSecondIface, "wrong ItestSecondIface\n");
    return S_OK;
}

static const IStaticWidgetVtbl StaticWidgetVtbl = {
    StaticWidget_QueryInterface,
    StaticWidget_AddRef,
    StaticWidget_Release,
    StaticWidget_GetTypeInfoCount,
    StaticWidget_GetTypeInfo,
    StaticWidget_GetIDsOfNames,
    StaticWidget_Invoke,
    StaticWidget_TestDual,
    StaticWidget_TestSecondIface
};

static IStaticWidget StaticWidget = { &StaticWidgetVtbl };

typedef struct KindaEnum
{
    IKindaEnumWidget IKindaEnumWidget_iface;
    LONG refs;
} KindaEnum;

static inline KindaEnum *impl_from_IKindaEnumWidget(IKindaEnumWidget *iface)
{
    return CONTAINING_RECORD(iface, KindaEnum, IKindaEnumWidget_iface);
}

static HRESULT register_current_module_typelib(void)
{
    WCHAR path[MAX_PATH];
    CHAR pathA[MAX_PATH];
    HRESULT hr;
    ITypeLib *typelib;

    GetModuleFileNameA(NULL, pathA, MAX_PATH);
    MultiByteToWideChar(CP_ACP, 0, pathA, -1, path, MAX_PATH);

    hr = LoadTypeLib(path, &typelib);
    if (SUCCEEDED(hr))
    {
        hr = RegisterTypeLib(typelib, path, NULL);
        ITypeLib_Release(typelib);
    }
    return hr;
}

static ITypeInfo *get_type_info(REFIID riid)
{
    ITypeInfo *pTypeInfo;
    ITypeLib *pTypeLib;
    HRESULT hr;

    hr = LoadRegTypeLib(&LIBID_TestTypelib, 2, 5, LOCALE_NEUTRAL, &pTypeLib);
    ok_ole_success(hr, LoadRegTypeLib);
    if (FAILED(hr))
        return NULL;

    hr = ITypeLib_GetTypeInfoOfGuid(pTypeLib, riid, &pTypeInfo);
    ITypeLib_Release(pTypeLib);
    ok_ole_success(hr, ITypeLib_GetTypeInfoOfGuid);
    if (FAILED(hr))
        return NULL;

    return pTypeInfo;
}

static IWidget *Widget_Create(void)
{
    Widget *This;
    ITypeInfo *pTypeInfo;
    HRESULT hr = E_FAIL;

    pTypeInfo = get_type_info(&IID_IWidget);
    if(!pTypeInfo)
        return NULL;

    This = HeapAlloc(GetProcessHeap(), 0, sizeof(*This));
    This->IWidget_iface.lpVtbl = &Widget_VTable;
    This->refs = 1;
    This->pDispatchUnknown = NULL;

    hr = CreateStdDispatch((IUnknown *)&This->IWidget_iface, This, pTypeInfo,
                           &This->pDispatchUnknown);
    ok_ole_success(hr, CreateStdDispatch);
    ITypeInfo_Release(pTypeInfo);

    if (SUCCEEDED(hr))
        return &This->IWidget_iface;
    else
    {
        HeapFree(GetProcessHeap(), 0, This);
        return NULL;
    }
}

static HRESULT WINAPI KindaEnum_QueryInterface(
    IKindaEnumWidget *iface,
    /* [in] */ REFIID riid,
    /* [iid_is][out] */ void __RPC_FAR *__RPC_FAR *ppvObject)
{
    if (IsEqualIID(riid, &IID_IKindaEnumWidget) || IsEqualIID(riid, &IID_IUnknown))
    {
        IKindaEnumWidget_AddRef(iface);
        *ppvObject = iface;
        return S_OK;
    }
    else
    {
        *ppvObject = NULL;
        return E_NOINTERFACE;
    }
}

static ULONG WINAPI KindaEnum_AddRef(
    IKindaEnumWidget *iface)
{
    KindaEnum *This = impl_from_IKindaEnumWidget(iface);

    return InterlockedIncrement(&This->refs);
}

static ULONG WINAPI KindaEnum_Release(
    IKindaEnumWidget *iface)
{
    KindaEnum *This = impl_from_IKindaEnumWidget(iface);
    ULONG refs = InterlockedDecrement(&This->refs);
    if (!refs)
    {
        memset(This, 0xcc, sizeof(*This));
        HeapFree(GetProcessHeap(), 0, This);
        trace("KindaEnumWidget destroyed!\n");
    }

    return refs;
}

static HRESULT WINAPI KindaEnum_Next(
    IKindaEnumWidget *iface,
    /* [out] */ IWidget __RPC_FAR *__RPC_FAR *widget)
{
    *widget = Widget_Create();
    if (*widget)
        return S_OK;
    else
        return E_OUTOFMEMORY;
}

static HRESULT WINAPI KindaEnum_Count(
    IKindaEnumWidget *iface,
    /* [out] */ ULONG __RPC_FAR *count)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI KindaEnum_Reset(
    IKindaEnumWidget *iface)
{
    return E_NOTIMPL;
}

static HRESULT WINAPI KindaEnum_Clone(
    IKindaEnumWidget *iface,
    /* [out] */ IKindaEnumWidget __RPC_FAR *__RPC_FAR *ppenum)
{
    return E_NOTIMPL;
}

static const IKindaEnumWidgetVtbl KindaEnumWidget_VTable =
{
    KindaEnum_QueryInterface,
    KindaEnum_AddRef,
    KindaEnum_Release,
    KindaEnum_Next,
    KindaEnum_Count,
    KindaEnum_Reset,
    KindaEnum_Clone
};

static IKindaEnumWidget *KindaEnumWidget_Create(void)
{
    KindaEnum *This;

    This = HeapAlloc(GetProcessHeap(), 0, sizeof(*This));
    if (!This) return NULL;
    This->IKindaEnumWidget_iface.lpVtbl = &KindaEnumWidget_VTable;
    This->refs = 1;
    return &This->IKindaEnumWidget_iface;
}

static HRESULT WINAPI NonOleAutomation_QueryInterface(INonOleAutomation *iface, REFIID riid, void **ppv)
{
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_INonOleAutomation))
    {
        *(INonOleAutomation **)ppv = iface;
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI NonOleAutomation_AddRef(INonOleAutomation *iface)
{
    return 2;
}

static ULONG WINAPI NonOleAutomation_Release(INonOleAutomation *iface)
{
    return 1;
}

static BSTR WINAPI NonOleAutomation_BstrRet(INonOleAutomation *iface)
{
    static const WCHAR wszTestString[] = {'T','h','i','s',' ','i','s',' ','a',' ','t','e','s','t',' ','s','t','r','i','n','g',0};
    return SysAllocString(wszTestString);
}

static HRESULT WINAPI NonOleAutomation_Error(INonOleAutomation *iface)
{
    return E_NOTIMPL;
}

static INonOleAutomationVtbl NonOleAutomation_VTable =
{
    NonOleAutomation_QueryInterface,
    NonOleAutomation_AddRef,
    NonOleAutomation_Release,
    NonOleAutomation_BstrRet,
    NonOleAutomation_Error
};

static INonOleAutomation NonOleAutomation = { &NonOleAutomation_VTable };

static ITypeInfo *NonOleAutomation_GetTypeInfo(void)
{
    ITypeLib *pTypeLib;
    HRESULT hr = LoadRegTypeLib(&LIBID_TestTypelib, 2, 5, LOCALE_NEUTRAL, &pTypeLib);
    ok_ole_success(hr, LoadRegTypeLib);
    if (SUCCEEDED(hr))
    {
        ITypeInfo *pTypeInfo;
        hr = ITypeLib_GetTypeInfoOfGuid(pTypeLib, &IID_INonOleAutomation, &pTypeInfo);
        ok_ole_success(hr, ITypeLib_GetTypeInfoOfGuid);
        ITypeLib_Release(pTypeLib);
        return pTypeInfo;
    }
    return NULL;
}

static void test_marshal_basetypes(IWidget *widget, IDispatch *disp)
{
    VARIANTARG arg[11];
    DISPPARAMS dispparams = {arg, NULL, ARRAY_SIZE(arg), 0};
    HRESULT hr;

    signed char c;
    short s;
    int i;
    hyper h;
    unsigned char uc;
    unsigned short us;
    unsigned int ui;
    MIDL_uhyper uh;
    float f;
    double d;
    STATE st;

    V_VT(&arg[10]) = VT_I1;     V_I1(&arg[10]) = 5;
    V_VT(&arg[9])  = VT_I2;     V_I2(&arg[9])  = -123;
    V_VT(&arg[8])  = VT_I4;     V_I4(&arg[8])  = -100000;
    V_VT(&arg[7])  = VT_I8;     V_I8(&arg[7])  = (LONGLONG)-100000 * 1000000;
    V_VT(&arg[6])  = VT_UI1;    V_UI1(&arg[6]) = 0;
    V_VT(&arg[5])  = VT_UI2;    V_UI2(&arg[5]) = 456;
    V_VT(&arg[4])  = VT_UI4;    V_UI4(&arg[4]) = 0xdeadbeef;
    V_VT(&arg[3])  = VT_UI8;    V_UI8(&arg[3]) = (ULONGLONG)1234567890 * 9876543210;
    V_VT(&arg[2])  = VT_R4;     V_R4(&arg[2])  = M_PI;
    V_VT(&arg[1])  = VT_R8;     V_R8(&arg[1])  = M_E;
    V_VT(&arg[0])  = VT_I4;     V_I4(&arg[0])  = STATE_WIDGETIFIED;
    hr = IDispatch_Invoke(disp, DISPID_TM_BASETYPES_IN, &IID_NULL, LOCALE_NEUTRAL,
            DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    hr = IWidget_basetypes_in(widget, 5, -123, -100000, (LONGLONG)-100000 * 1000000, 0, 456,
            0xdeadbeef, (ULONGLONG)1234567890 * 9876543210, M_PI, M_E, STATE_WIDGETIFIED);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    c = s = i = h = uc = us = ui = uh = f = d = st = 0;

    V_VT(&arg[10]) = VT_BYREF|VT_I1;  V_I1REF(&arg[10]) = &c;
    V_VT(&arg[9])  = VT_BYREF|VT_I2;  V_I2REF(&arg[9])  = &s;
    V_VT(&arg[8])  = VT_BYREF|VT_I4;  V_I4REF(&arg[8])  = &i;
    V_VT(&arg[7])  = VT_BYREF|VT_I8;  V_I8REF(&arg[7])  = &h;
    V_VT(&arg[6])  = VT_BYREF|VT_UI1; V_UI1REF(&arg[6]) = &uc;
    V_VT(&arg[5])  = VT_BYREF|VT_UI2; V_UI2REF(&arg[5]) = &us;
    V_VT(&arg[4])  = VT_BYREF|VT_UI4; V_UI4REF(&arg[4]) = &ui;
    V_VT(&arg[3])  = VT_BYREF|VT_UI8; V_UI8REF(&arg[3]) = &uh;
    V_VT(&arg[2])  = VT_BYREF|VT_R4;  V_R4REF(&arg[2])  = &f;
    V_VT(&arg[1])  = VT_BYREF|VT_R8;  V_R8REF(&arg[1])  = &d;
    V_VT(&arg[0])  = VT_BYREF|VT_I4;  V_I4REF(&arg[0])  = (int *)&st;
    hr = IDispatch_Invoke(disp, DISPID_TM_BASETYPES_OUT, &IID_NULL, LOCALE_NEUTRAL,
            DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(c == 10, "Got char %d.\n", c);
    ok(s == -321, "Got short %d.\n", s);
    ok(i == -200000, "Got int %d.\n", i);
    ok(h == (LONGLONG)-200000 * 1000000L, "Got hyper %s.\n", wine_dbgstr_longlong(h));
    ok(uc == 254, "Got unsigned char %u.\n", uc);
    ok(us == 256, "Got unsigned short %u.\n", us);
    ok(ui == 0xf00dfade, "Got unsigned int %i.\n", ui);
    ok(uh == ((((ULONGLONG)0xabcdef01) << 32) | (ULONGLONG)0x23456789),
            "Got unsigned hyper %s.\n", wine_dbgstr_longlong(uh));
    ok(f == (float)M_LN2, "Got float %f.\n", f);
    ok(d == M_LN10, "Got double %f.\n", d);
    ok(st == STATE_UNWIDGETIFIED, "Got state %u.\n", st);

    c = s = i = h = uc = us = ui = uh = f = d = st = 0;

    hr = IWidget_basetypes_out(widget, &c, &s, &i, &h, &uc, &us, &ui, &uh, &f, &d, &st);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(c == 10, "Got char %d.\n", c);
    ok(s == -321, "Got short %d.\n", s);
    ok(i == -200000, "Got int %d.\n", i);
    ok(h == (LONGLONG)-200000 * 1000000L, "Got hyper %s.\n", wine_dbgstr_longlong(h));
    ok(uc == 254, "Got unsigned char %u.\n", uc);
    ok(us == 256, "Got unsigned short %u.\n", us);
    ok(ui == 0xf00dfade, "Got unsigned int %i.\n", ui);
    ok(uh == ((((ULONGLONG)0xabcdef01) << 32) | (ULONGLONG)0x23456789),
            "Got unsigned hyper %s.\n", wine_dbgstr_longlong(uh));
    ok(f == (float)M_LN2, "Got float %f.\n", f);
    ok(d == M_LN10, "Got double %f.\n", d);
    ok(st == STATE_UNWIDGETIFIED, "Got state %u.\n", st);
}

static void test_marshal_pointer(IWidget *widget, IDispatch *disp)
{
    VARIANTARG arg[3];
    DISPPARAMS dispparams = {arg, NULL, ARRAY_SIZE(arg), 0};
    int in, out, in_out, *in_ptr, *out_ptr, *in_out_ptr;
    HRESULT hr;

    testmode = 0;

    in = 123;
    out = 456;
    in_out = 789;
    V_VT(&arg[2]) = VT_BYREF|VT_I4; V_I4REF(&arg[2]) = &in;
    V_VT(&arg[1]) = VT_BYREF|VT_I4; V_I4REF(&arg[1]) = &out;
    V_VT(&arg[0]) = VT_BYREF|VT_I4; V_I4REF(&arg[0]) = &in_out;
    hr = IDispatch_Invoke(disp, DISPID_TM_INT_PTR, &IID_NULL, LOCALE_NEUTRAL,
            DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(in == 987, "Got [in] %d.\n", in);
    ok(out == 654, "Got [out] %d.\n", out);
    ok(in_out == 321, "Got [in, out] %d.\n", in_out);

    testmode = 1;

    in = 123;
    out = 456;
    in_out = 789;
    hr = IWidget_int_ptr(widget, &in, &out, &in_out);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(in == 123, "Got [in] %d.\n", in);
    ok(out == 654, "Got [out] %d.\n", out);
    ok(in_out == 321, "Got [in, out] %d.\n", in_out);

if (0) {
    out = in_out = -1;
    hr = IWidget_int_ptr(widget, NULL, &out, &in_out);
    ok(hr == HRESULT_FROM_WIN32(RPC_X_NULL_REF_POINTER), "Got hr %#x.\n", hr);
    ok(!out, "[out] parameter should have been cleared.\n");
    ok(in_out == -1, "[in, out] parameter should not have been cleared.\n");

    in = in_out = -1;
    hr = IWidget_int_ptr(widget, &in, NULL, &in_out);
    ok(hr == HRESULT_FROM_WIN32(RPC_X_NULL_REF_POINTER), "Got hr %#x.\n", hr);
    ok(in == -1, "[in] parameter should not have been cleared.\n");
    ok(in_out == -1, "[in, out] parameter should not have been cleared.\n");

    in = out = -1;
    hr = IWidget_int_ptr(widget, &in, &out, NULL);
    ok(hr == HRESULT_FROM_WIN32(RPC_X_NULL_REF_POINTER), "Got hr %#x.\n", hr);
    ok(in == -1, "[in] parameter should not have been cleared.\n");
    ok(!out, "[out] parameter should have been cleared.\n");
}

    /* We can't test Invoke() with double pointers, as it is not possible to fit
     * more than one level of indirection into a VARIANTARG. */

    testmode = 0;
    in_ptr = out_ptr = in_out_ptr = NULL;
    hr = IWidget_int_ptr_ptr(widget, &in_ptr, &out_ptr, &in_out_ptr);
    ok(hr == S_OK, "Got hr %#x\n", hr);
    ok(!in_ptr, "Got [in] %p.\n", in_ptr);
    ok(!out_ptr, "Got [out] %p.\n", out_ptr);
    ok(!in_out_ptr, "Got [in, out] %p.\n", in_out_ptr);

if (0) {
    testmode = 1;
    hr = IWidget_int_ptr_ptr(widget, &in_ptr, &out_ptr, &in_out_ptr);
    ok(hr == S_OK, "Got hr %#x\n", hr);
    ok(*out_ptr == 654, "Got [out] %d.\n", *out_ptr);
    ok(*in_out_ptr == 321, "Got [in, out] %d.\n", *in_out_ptr);
    CoTaskMemFree(out_ptr);
    CoTaskMemFree(in_out_ptr);

    testmode = 2;
    in = 123;
    out = 456;
    in_out = 789;
    in_ptr = &in;
    out_ptr = &out;
    in_out_ptr = &in_out;
    hr = IWidget_int_ptr_ptr(widget, &in_ptr, &out_ptr, &in_out_ptr);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(out_ptr != &out, "[out] ptr should have changed.\n");
    ok(in_out_ptr == &in_out, "[in, out] ptr should not have changed.\n");
    ok(*out_ptr == 654, "Got [out] %d.\n", *out_ptr);
    ok(*in_out_ptr == 321, "Got [in, out] %d.\n", *in_out_ptr);
}

    testmode = 3;
    in_ptr = out_ptr = NULL;
    in_out = 789;
    in_out_ptr = &in_out;
    hr = IWidget_int_ptr_ptr(widget, &in_ptr, &out_ptr, &in_out_ptr);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!in_out_ptr, "Got [in, out] %p.\n", in_out_ptr);

if (0) {
    out_ptr = &out;
    in_out_ptr = &in_out;
    hr = IWidget_int_ptr_ptr(widget, NULL, &out_ptr, &in_out_ptr);
    ok(hr == HRESULT_FROM_WIN32(RPC_X_NULL_REF_POINTER), "Got hr %#x.\n", hr);
    ok(!out_ptr, "[out] parameter should have been cleared.\n");
    ok(in_out_ptr == &in_out, "[in, out] parameter should not have been cleared.\n");

    in_ptr = &in;
    in_out_ptr = &in_out;
    hr = IWidget_int_ptr_ptr(widget, &in_ptr, NULL, &in_out_ptr);
    ok(hr == HRESULT_FROM_WIN32(RPC_X_NULL_REF_POINTER), "Got hr %#x.\n", hr);
    ok(in_ptr == &in, "[in] parameter should not have been cleared.\n");
    ok(in_out_ptr == &in_out, "[in, out] parameter should not have been cleared.\n");

    in_ptr = &in;
    out_ptr = &out;
    hr = IWidget_int_ptr_ptr(widget, &in_ptr, &out_ptr, NULL);
    ok(hr == HRESULT_FROM_WIN32(RPC_X_NULL_REF_POINTER), "Got hr %#x.\n", hr);
    ok(in_ptr == &in, "[in] parameter should not have been cleared.\n");
    ok(!out_ptr, "[out] parameter should have been cleared.\n");
}
}

static void test_marshal_iface(IWidget *widget, IDispatch *disp)
{
    VARIANTARG arg[3];
    DISPPARAMS dispparams = {arg, NULL, ARRAY_SIZE(arg), 0};
    ISomethingFromDispatch *sfd1, *sfd2, *sfd3, *proxy_sfd, *sfd_in, *sfd_out, *sfd_in_out;
    IUnknown *proxy_unk, *proxy_unk2, *unk_in, *unk_out, *unk_in_out;
    IDispatch *proxy_disp;
    HRESULT hr;

    testmode = 0;
    sfd1 = create_disp_obj();
    sfd2 = create_disp_obj();
    sfd3 = create_disp_obj();
    hr = IWidget_iface_in(widget, (IUnknown *)create_disp_obj(),
            (IDispatch *)create_disp_obj(), create_disp_obj());
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    release_iface(sfd1);
    release_iface(sfd2);
    release_iface(sfd3);

    testmode = 1;
    hr = IWidget_iface_in(widget, NULL, NULL, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    testmode = 0;
    proxy_unk = (IUnknown *)0xdeadbeef;
    proxy_disp = (IDispatch *)0xdeadbeef;
    proxy_sfd = (ISomethingFromDispatch *)0xdeadbeef;
    hr = IWidget_iface_out(widget, &proxy_unk, &proxy_disp, &proxy_sfd);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    check_iface_marshal(proxy_unk, proxy_disp, proxy_sfd);
    release_iface(proxy_unk);
    release_iface(proxy_disp);
    release_iface(proxy_sfd);

if (0) {
    testmode = 1;
    hr = IWidget_iface_out(widget, &proxy_unk, &proxy_disp, &proxy_sfd);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!proxy_unk, "Got unexpected proxy %p.\n", proxy_unk);
    ok(!proxy_disp, "Got unexpected proxy %p.\n", proxy_disp);
    ok(!proxy_sfd, "Got unexpected proxy %p.\n", proxy_sfd);
}

    testmode = 0;
    sfd_in = sfd1 = create_disp_obj();
    sfd_out = sfd2 = create_disp_obj();
    sfd_in_out = sfd3 = create_disp_obj();
    hr = IWidget_iface_ptr(widget, &sfd_in, &sfd_out, &sfd_in_out);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(sfd_in == sfd1, "[in] parameter should not have changed.\n");
    ok(!sfd_out, "[out] parameter should have been cleared.\n");
    ok(sfd_in_out == sfd3, "[in, out] parameter should not have changed.\n");
    release_iface(sfd1);
    release_iface(sfd2);
todo_wine
    release_iface(sfd3);

    testmode = 1;
    sfd_in = sfd1 = create_disp_obj();
    sfd_in_out = sfd3 = create_disp_obj();
    ISomethingFromDispatch_AddRef(sfd_in_out);
    hr = IWidget_iface_ptr(widget, &sfd_in, &sfd_out, &sfd_in_out);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = ISomethingFromDispatch_anotherfn(sfd_out);
    ok(hr == 0x01234567, "Got hr %#x.\n", hr);
    ok(sfd_in_out != sfd3, "[in, out] parameter should have changed.\n");
    hr = ISomethingFromDispatch_anotherfn(sfd_in_out);
    ok(hr == 0x01234567, "Got hr %#x.\n", hr);
    release_iface(sfd_out);
    release_iface(sfd_in_out);
    release_iface(sfd1);
todo_wine
    release_iface(sfd3);

    testmode = 2;
    sfd_in = sfd_out = sfd_in_out = NULL;
    hr = IWidget_iface_ptr(widget, &sfd_in, &sfd_out, &sfd_in_out);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!sfd_out, "[out] parameter should not have been set.\n");
    hr = ISomethingFromDispatch_anotherfn(sfd_in_out);
    ok(hr == 0x01234567, "Got hr %#x.\n", hr);
    release_iface(sfd_in_out);

    testmode = 3;
    sfd_in = sfd_out = NULL;
    sfd_in_out = sfd3 = create_disp_obj();
    ISomethingFromDispatch_AddRef(sfd_in_out);
    hr = IWidget_iface_ptr(widget, &sfd_in, &sfd_out, &sfd_in_out);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!sfd_in_out, "Got [in, out] %p.\n", sfd_in_out);
todo_wine
    release_iface(sfd3);

    /* Test with Invoke(). Note that since we pass VT_UNKNOWN, we don't get our
     * interface back, but rather an IUnknown. */

    testmode = 0;
    sfd1 = create_disp_obj();
    sfd2 = create_disp_obj();
    sfd3 = create_disp_obj();

    V_VT(&arg[2]) = VT_UNKNOWN;  V_UNKNOWN(&arg[2]) = (IUnknown *)sfd1;
    V_VT(&arg[1]) = VT_UNKNOWN;  V_UNKNOWN(&arg[1]) = (IUnknown *)sfd2;
    V_VT(&arg[0]) = VT_UNKNOWN;  V_UNKNOWN(&arg[0]) = (IUnknown *)sfd3;
    hr = IDispatch_Invoke(disp, DISPID_TM_IFACE_IN, &IID_NULL, LOCALE_NEUTRAL,
            DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    V_VT(&arg[2]) = VT_DISPATCH; V_DISPATCH(&arg[2]) = (IDispatch *)sfd1;
    V_VT(&arg[1]) = VT_DISPATCH; V_DISPATCH(&arg[1]) = (IDispatch *)sfd2;
    V_VT(&arg[0]) = VT_DISPATCH; V_DISPATCH(&arg[0]) = (IDispatch *)sfd3;
    hr = IDispatch_Invoke(disp, DISPID_TM_IFACE_IN, &IID_NULL, LOCALE_NEUTRAL,
            DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    release_iface(sfd1);
    release_iface(sfd2);
    release_iface(sfd3);

    testmode = 1;
    V_VT(&arg[2]) = VT_UNKNOWN;  V_UNKNOWN(&arg[2]) = NULL;
    V_VT(&arg[1]) = VT_UNKNOWN;  V_UNKNOWN(&arg[1]) = NULL;
    V_VT(&arg[0]) = VT_UNKNOWN;  V_UNKNOWN(&arg[0]) = NULL;
    hr = IDispatch_Invoke(disp, DISPID_TM_IFACE_IN, &IID_NULL, LOCALE_NEUTRAL,
            DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    testmode = 0;
    proxy_unk = proxy_unk2 = NULL;
    proxy_disp = NULL;
    V_VT(&arg[2]) = VT_UNKNOWN|VT_BYREF;  V_UNKNOWNREF(&arg[2]) = &proxy_unk;
    V_VT(&arg[1]) = VT_DISPATCH|VT_BYREF; V_DISPATCHREF(&arg[1]) = &proxy_disp;
    V_VT(&arg[0]) = VT_UNKNOWN|VT_BYREF;  V_UNKNOWNREF(&arg[0]) = &proxy_unk2;
    hr = IDispatch_Invoke(disp, DISPID_TM_IFACE_OUT, &IID_NULL, LOCALE_NEUTRAL,
            DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
todo_wine
    ok(hr == S_OK, "Got hr %#x.\n", hr);
if (hr == S_OK) {
    hr = IUnknown_QueryInterface(proxy_unk2, &IID_ISomethingFromDispatch, (void **)&proxy_sfd);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    check_iface_marshal(proxy_unk, proxy_disp, proxy_sfd);
    ISomethingFromDispatch_Release(proxy_sfd);
    release_iface(proxy_unk);
    release_iface(proxy_disp);
    release_iface(proxy_unk2);
}

    testmode = 1;
    proxy_unk = proxy_unk2 = NULL;
    proxy_disp = NULL;
    hr = IDispatch_Invoke(disp, DISPID_TM_IFACE_OUT, &IID_NULL, LOCALE_NEUTRAL,
            DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
todo_wine
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!proxy_unk, "Got unexpected proxy %p.\n", proxy_unk);
    ok(!proxy_disp, "Got unexpected proxy %p.\n", proxy_disp);
    ok(!proxy_unk2, "Got unexpected proxy %p.\n", proxy_unk2);

    testmode = 0;
    sfd1 = create_disp_obj();
    sfd3 = create_disp_obj();
    unk_in = (IUnknown *)sfd1;
    unk_out = NULL;
    unk_in_out = (IUnknown *)sfd3;
    V_VT(&arg[2]) = VT_UNKNOWN|VT_BYREF; V_UNKNOWNREF(&arg[2]) = &unk_in;
    V_VT(&arg[1]) = VT_UNKNOWN|VT_BYREF; V_UNKNOWNREF(&arg[1]) = &unk_out;
    V_VT(&arg[0]) = VT_UNKNOWN|VT_BYREF; V_UNKNOWNREF(&arg[0]) = &unk_in_out;
    hr = IDispatch_Invoke(disp, DISPID_TM_IFACE_PTR, &IID_NULL, LOCALE_NEUTRAL,
            DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
todo_wine
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(unk_in == (IUnknown *)sfd1, "[in] parameter should not have changed.\n");
    ok(!unk_out, "[out] parameter should have been cleared.\n");
    ok(unk_in_out == (IUnknown *)sfd3, "[in, out] parameter should not have changed.\n");
    release_iface(sfd1);
    release_iface(sfd3);

    testmode = 1;
    sfd1 = create_disp_obj();
    sfd3 = create_disp_obj();
    unk_in = (IUnknown *)sfd1;
    unk_out = NULL;
    unk_in_out = (IUnknown *)sfd3;
    IUnknown_AddRef(unk_in_out);
    hr = IDispatch_Invoke(disp, DISPID_TM_IFACE_PTR, &IID_NULL, LOCALE_NEUTRAL,
            DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
todo_wine
    ok(hr == S_OK, "Got hr %#x.\n", hr);

if (hr == S_OK) {
    hr = IUnknown_QueryInterface(unk_out, &IID_ISomethingFromDispatch, (void **)&sfd_out);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = ISomethingFromDispatch_anotherfn(sfd_out);
    ok(hr == 0x01234567, "Got hr %#x.\n", hr);
    ISomethingFromDispatch_Release(sfd_out);

    ok(unk_in_out != (IUnknown *)sfd3, "[in, out] parameter should have changed.\n");
    hr = IUnknown_QueryInterface(unk_in_out, &IID_ISomethingFromDispatch, (void **)&sfd_in_out);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = ISomethingFromDispatch_anotherfn(sfd_in_out);
    ok(hr == 0x01234567, "Got hr %#x.\n", hr);
    ISomethingFromDispatch_Release(sfd_in_out);

    release_iface(unk_out);
    release_iface(unk_in_out);
    release_iface(sfd1);
    release_iface(sfd3);
}

    testmode = 2;
    unk_in = unk_out = unk_in_out = NULL;
    hr = IDispatch_Invoke(disp, DISPID_TM_IFACE_PTR, &IID_NULL, LOCALE_NEUTRAL,
            DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
todo_wine
    ok(hr == S_OK, "Got hr %#x.\n", hr);

    ok(!unk_out, "[out] parameter should not have been set.\n");
if (hr == S_OK) {
    hr = IUnknown_QueryInterface(unk_in_out, &IID_ISomethingFromDispatch, (void **)&sfd_in_out);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    hr = ISomethingFromDispatch_anotherfn(sfd_in_out);
    ok(hr == 0x01234567, "Got hr %#x.\n", hr);
    ISomethingFromDispatch_Release(sfd_in_out);

    release_iface(unk_in_out);
}

    testmode = 3;
    unk_in = unk_out = NULL;
    sfd3 = create_disp_obj();
    unk_in_out = (IUnknown *)sfd3;
    IUnknown_AddRef(unk_in_out);
    hr = IDispatch_Invoke(disp, DISPID_TM_IFACE_PTR, &IID_NULL, LOCALE_NEUTRAL,
            DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
todo_wine {
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(!unk_in_out, "[in, out] parameter should have been cleared.\n");
    release_iface(sfd3);
}
}

static void test_marshal_bstr(IWidget *widget, IDispatch *disp)
{
    VARIANTARG arg[4];
    DISPPARAMS dispparams = {arg, NULL, ARRAY_SIZE(arg), 0};
    BSTR in, out, in_ptr, in_out;
    HRESULT hr;
    UINT len;

    testmode = 0;
    in = SysAllocStringLen(test_bstr1, ARRAY_SIZE(test_bstr1));
    out = NULL;
    in_ptr = SysAllocString(test_bstr2);
    in_out = SysAllocString(test_bstr3);

    V_VT(&arg[3]) = VT_BSTR;            V_BSTR(&arg[3])    = in;
    V_VT(&arg[2]) = VT_BSTR|VT_BYREF;   V_BSTRREF(&arg[2]) = &out;
    V_VT(&arg[1]) = VT_BSTR|VT_BYREF;   V_BSTRREF(&arg[1]) = &in_ptr;
    V_VT(&arg[0]) = VT_BSTR|VT_BYREF;   V_BSTRREF(&arg[0]) = &in_out;
    hr = IDispatch_Invoke(disp, DISPID_TM_BSTR, &IID_NULL, LOCALE_NEUTRAL,
            DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(in[1] == test_bstr1[1], "[in] parameter should not be changed.\n");
    ok(in_ptr[1] == 'X', "[in] pointer should be changed.\n");
    ok(in_out[1] == 'X', "[in, out] parameter should be changed.\n");
    len = SysStringLen(out);
    ok(len == lstrlenW(test_bstr4), "Got wrong length %d.\n", len);
    ok(!memcmp(out, test_bstr4, len), "Got string %s.\n", wine_dbgstr_wn(out, len));

    in[1] = test_bstr1[1];
    in_ptr[1] = test_bstr2[1];
    in_out[1] = test_bstr3[1];
    SysFreeString(out);
    out = (BSTR)0xdeadbeef;
    hr = IWidget_bstr(widget, in, &out, &in_ptr, &in_out);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(in[1] == test_bstr1[1], "[in] parameter should not be changed.\n");
    ok(in_ptr[1] == test_bstr2[1], "[in] pointer should not be changed.\n");
    ok(in_out[1] == 'X', "[in, out] parameter should be changed.\n");
    len = SysStringLen(out);
    ok(len == lstrlenW(test_bstr4), "Got wrong length %d.\n", len);
    ok(!memcmp(out, test_bstr4, len), "Got string %s.\n", wine_dbgstr_wn(out, len));
    SysFreeString(in);
    SysFreeString(out);
    SysFreeString(in_ptr);
    SysFreeString(in_out);

    testmode = 1;
    out = in_ptr = in_out = NULL;
    hr = IWidget_bstr(widget, NULL, &out, &in_ptr, &in_out);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
}

static void test_marshal_variant(IWidget *widget, IDispatch *disp)
{
    VARIANTARG arg[4];
    DISPPARAMS dispparams = {arg, NULL, ARRAY_SIZE(arg), 0};
    VARIANT out, in_ptr, in_out;
    HRESULT hr;
    BSTR bstr;

    testmode = 0;
    V_VT(&out) = VT_I4;
    V_I4(&out) = 1;
    V_VT(&in_ptr) = VT_I4;
    V_I4(&in_ptr) = -1;
    V_VT(&in_out) = VT_BSTR;
    V_BSTR(&in_out) = bstr = SysAllocString(test_bstr2);

    V_VT(&arg[3]) = VT_CY;
    V_CY(&arg[3]).Hi = 0xdababe;
    V_CY(&arg[3]).Lo = 0xdeadbeef;
    V_VT(&arg[2]) = VT_VARIANT|VT_BYREF; V_VARIANTREF(&arg[2]) = &out;
    V_VT(&arg[1]) = VT_VARIANT|VT_BYREF; V_VARIANTREF(&arg[1]) = &in_ptr;
    V_VT(&arg[0]) = VT_VARIANT|VT_BYREF; V_VARIANTREF(&arg[0]) = &in_out;
    hr = IDispatch_Invoke(disp, DISPID_TM_VARIANT, &IID_NULL, LOCALE_NEUTRAL,
            DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(V_VT(&arg[3]) == VT_CY, "Got wrong type %u.\n", V_VT(&arg[3]));
    ok(V_VT(&out) == VT_UI1, "Got wrong type %u.\n", V_VT(&out));
    ok(V_UI1(&out) == 3, "Got wrong value %d.\n", V_UI1(&out));
    VariantClear(&out);
    ok(V_VT(&in_ptr) == VT_I2, "Got wrong type %u.\n", V_VT(&in_ptr));
    ok(V_I2(&in_ptr) == 4, "Got wrong value %d.\n", V_I1(&in_ptr));
    ok(V_VT(&in_out) == VT_I1, "Got wrong type %u.\n", V_VT(&in_out));
    ok(V_I1(&in_out) == 5, "Got wrong value %d.\n", V_I1(&in_out));

    testmode = 1;
    V_VT(&out) = VT_I4;
    V_I4(&out) = 1;
    V_VT(&in_ptr) = VT_I4;
    V_I4(&in_ptr) = -1;
    V_VT(&in_out) = VT_BSTR;
    V_BSTR(&in_out) = bstr = SysAllocString(test_bstr2);
    hr = IWidget_variant(widget, arg[3], &out, &in_ptr, &in_out);
    ok(hr == S_OK, "Got hr %#x.\n", hr);
    ok(V_VT(&arg[3]) == VT_CY, "Got wrong type %u.\n", V_VT(&arg[3]));
    ok(V_VT(&out) == VT_UI1, "Got wrong type %u.\n", V_VT(&out));
    ok(V_UI1(&out) == 3, "Got wrong value %d.\n", V_UI1(&out));
    ok(V_VT(&in_ptr) == VT_I4, "Got wrong type %u.\n", V_VT(&in_ptr));
    ok(V_I2(&in_ptr) == -1, "Got wrong value %d.\n", V_I1(&in_ptr));
    ok(V_VT(&in_out) == VT_I1, "Got wrong type %u.\n", V_VT(&in_out));
    ok(V_I1(&in_out) == 5, "Got wrong value %d.\n", V_I1(&in_out));
}

static void test_typelibmarshal(void)
{
    static const WCHAR szCat[] = { 'C','a','t',0 };
    static const WCHAR szTestTest[] = { 'T','e','s','t','T','e','s','t',0 };
    static const WCHAR szSuperman[] = { 'S','u','p','e','r','m','a','n',0 };
    HRESULT hr;
    IKindaEnumWidget *pKEW = KindaEnumWidget_Create();
    IWidget *pWidget;
    IStream *pStream;
    IDispatch *pDispatch;
    static const LARGE_INTEGER ullZero;
    EXCEPINFO excepinfo;
    VARIANT varresult;
    DISPID dispidNamed = DISPID_PROPERTYPUT;
    DISPPARAMS dispparams;
    VARIANTARG vararg[4];
    STATE the_state;
    HANDLE thread;
    DWORD tid;
    BSTR bstr;
    ITypeInfo *pTypeInfo;
    MYSTRUCT mystruct;
    MYSTRUCT mystructArray[5];
    UINT uval;

    ok(pKEW != NULL, "Widget creation failed\n");

    hr = CreateStreamOnHGlobal(NULL, TRUE, &pStream);
    ok_ole_success(hr, CreateStreamOnHGlobal);
    tid = start_host_object(pStream, &IID_IKindaEnumWidget, (IUnknown *)pKEW, MSHLFLAGS_NORMAL, &thread);
    IKindaEnumWidget_Release(pKEW);

    IStream_Seek(pStream, ullZero, STREAM_SEEK_SET, NULL);
    hr = CoUnmarshalInterface(pStream, &IID_IKindaEnumWidget, (void **)&pKEW);
#ifndef __i386__
    todo_wine
#endif
    ok_ole_success(hr, CoUnmarshalInterface);
    IStream_Release(pStream);
    if (FAILED(hr))
    {
        end_host_object(tid, thread);
        return;
    }

    hr = IKindaEnumWidget_Next(pKEW, &pWidget);
    ok_ole_success(hr, IKindaEnumWidget_Next);

    IKindaEnumWidget_Release(pKEW);

    /* call GetTypeInfoCount (direct) */
    hr = IWidget_GetTypeInfoCount(pWidget, &uval);
    ok_ole_success(hr, IWidget_GetTypeInfoCount);
    hr = IWidget_GetTypeInfoCount(pWidget, &uval);
    ok_ole_success(hr, IWidget_GetTypeInfoCount);

    hr = IWidget_QueryInterface(pWidget, &IID_IDispatch, (void **)&pDispatch);
    ok_ole_success(hr, IWidget_QueryInterface);

    /* call put_Name */
    VariantInit(&vararg[0]);
    dispparams.cNamedArgs = 1;
    dispparams.rgdispidNamedArgs = &dispidNamed;
    dispparams.cArgs = 1;
    dispparams.rgvarg = vararg;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_NAME, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_PROPERTYPUT, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, IDispatch_Invoke);
    ok(excepinfo.wCode == 0x0 && excepinfo.scode == S_OK,
        "EXCEPINFO differs from expected: wCode = 0x%x, scode = 0x%08x\n",
        excepinfo.wCode, excepinfo.scode);
    VariantClear(&varresult);

    /* call put_Name (direct) */
    bstr = SysAllocString(szSuperman);
    hr = IWidget_put_Name(pWidget, bstr);
    ok_ole_success(hr, IWidget_put_Name);
    SysFreeString(bstr);

    /* call get_Name */
    dispparams.cNamedArgs = 0;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.cArgs = 0;
    dispparams.rgvarg = NULL;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_NAME, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_PROPERTYGET, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, IDispatch_Invoke);
    ok(excepinfo.wCode == 0x0 && excepinfo.scode == S_OK,
        "EXCEPINFO differs from expected: wCode = 0x%x, scode = 0x%08x\n",
        excepinfo.wCode, excepinfo.scode);
    trace("Name = %s\n", wine_dbgstr_w(V_BSTR(&varresult)));
    VariantClear(&varresult);

    /* call get_Name (direct) */
    bstr = (void *)0xdeadbeef;
    hr = IWidget_get_Name(pWidget, &bstr);
    ok_ole_success(hr, IWidget_get_Name);
    ok(!lstrcmpW(bstr, szCat), "IWidget_get_Name should have returned string \"Cat\" instead of %s\n", wine_dbgstr_w(bstr));
    SysFreeString(bstr);

    /* call DoSomething */
    VariantInit(&vararg[0]);
    VariantInit(&vararg[1]);
    V_VT(&vararg[1]) = VT_R8;
    V_R8(&vararg[1]) = 3.141;
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 2;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = vararg;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_DOSOMETHING, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_METHOD, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, IDispatch_Invoke);
    ok(V_VT(&varresult) == VT_EMPTY, "varresult should be VT_EMPTY\n");
    VariantClear(&varresult);

    /* call get_State */
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 0;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = NULL;
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_STATE, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_PROPERTYGET, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, IDispatch_Invoke);
    ok((V_VT(&varresult) == VT_I4) && (V_I4(&varresult) == STATE_WIDGETIFIED), "Return val mismatch\n");

    /* call get_State (direct) */
    hr = IWidget_get_State(pWidget, &the_state);
    ok_ole_success(hr, IWidget_get_state);
    ok(the_state == STATE_WIDGETIFIED, "should have returned WIDGET_WIDGETIFIED instead of %d\n", the_state);

    /* call put_State */
    the_state = STATE_WIDGETIFIED;
    VariantInit(&vararg[0]);
    V_VT(&vararg[0]) = VT_BYREF|VT_I4;
    V_I4REF(&vararg[0]) = (int *)&the_state;
    dispparams.cNamedArgs = 1;
    dispparams.cArgs = 1;
    dispparams.rgdispidNamedArgs = &dispidNamed;
    dispparams.rgvarg = vararg;
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_STATE, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_PROPERTYPUT, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, IDispatch_Invoke);

    /* call Map */
    bstr = SysAllocString(szTestTest);
    VariantInit(&vararg[0]);
    V_VT(&vararg[0]) = VT_BYREF|VT_BSTR;
    V_BSTRREF(&vararg[0]) = &bstr;
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 1;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = vararg;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_MAP, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_METHOD, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, IDispatch_Invoke);
    ok(V_VT(&varresult) == VT_BSTR, "Return value should be of type BSTR instead of %d\n", V_VT(&varresult));
    ok(!lstrcmpW(V_BSTR(&varresult), szTestTest), "Return value should have been \"TestTest\" instead of %s\n", wine_dbgstr_w(V_BSTR(&varresult)));
    VariantClear(&varresult);
    SysFreeString(bstr);

    /* call SetOleColor with large negative VT_I4 param */
    VariantInit(&vararg[0]);
    V_VT(&vararg[0]) = VT_I4;
    V_I4(&vararg[0]) = 0x80000005;
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 1;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = vararg;
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_SETOLECOLOR, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_METHOD, &dispparams, NULL, &excepinfo, NULL);
    ok_ole_success(hr, IDispatch_Invoke);

    /* call GetOleColor */
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 0;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = NULL;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_GETOLECOLOR, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_METHOD, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, IDispatch_Invoke);
    VariantClear(&varresult);

    /* call StructArgs (direct) */
    mystruct = MYSTRUCT_BYPTR;
    memcpy(mystructArray, MYSTRUCT_ARRAY, sizeof(mystructArray));
    hr = IWidget_StructArgs(pWidget, MYSTRUCT_BYVAL, &mystruct, mystructArray);
    ok_ole_success(hr, IWidget_StructArgs);

    /* call Clone */
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 0;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = NULL;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_CLONE, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_PROPERTYGET, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, IDispatch_Invoke);
    ok(V_VT(&varresult) == VT_DISPATCH, "vt %x\n", V_VT(&varresult));
    VariantClear(&varresult);

    /* call CloneInterface */
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 0;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = NULL;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_CLONEINTERFACE, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_PROPERTYGET, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, IDispatch_Invoke);
    ok(V_VT(&varresult) == VT_DISPATCH, "vt %x\n", V_VT(&varresult));
    VariantClear(&varresult);

    /* call CloneDispatch with automatic value getting */
    V_VT(&vararg[0]) = VT_I2;
    V_I2(&vararg[0]) = 1;
    dispparams.cNamedArgs = 0;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.cArgs = 1;
    dispparams.rgvarg = vararg;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_CLONEDISPATCH, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_PROPERTYGET, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, IDispatch_Invoke);

    ok(excepinfo.wCode == 0x0 && excepinfo.scode == S_OK,
        "EXCEPINFO differs from expected: wCode = 0x%x, scode = 0x%08x\n",
        excepinfo.wCode, excepinfo.scode);

    ok(V_VT(&varresult) == VT_I2, "V_VT(&varresult) was %d instead of VT_I2\n", V_VT(&varresult));
    ok(V_I2(&varresult) == 1234, "V_I2(&varresult) was %d instead of 1234\n", V_I2(&varresult));
    VariantClear(&varresult);

    /* call CloneCoclass */
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 0;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = NULL;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_CLONECOCLASS, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_PROPERTYGET, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, IDispatch_Invoke);

    ok(excepinfo.wCode == 0x0 && excepinfo.scode == S_OK,
       "EXCEPINFO differs from expected: wCode = 0x%x, scode = 0x%08x\n",
       excepinfo.wCode, excepinfo.scode);

    ok(V_VT(&varresult) == VT_DISPATCH, "V_VT(&varresult) was %d instead of VT_DISPATCH\n", V_VT(&varresult));
    ok(V_DISPATCH(&varresult) != NULL, "expected V_DISPATCH(&varresult) != NULL\n");

    /* call Coclass with VT_DISPATCH type */
    vararg[0] = varresult;
    dispparams.cNamedArgs = 0;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.cArgs = 1;
    dispparams.rgvarg = vararg;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_COCLASS, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_METHOD, &dispparams, &varresult, &excepinfo, NULL);
    todo_wine ok_ole_success(hr, IDispatch_Invoke);
    ok(excepinfo.wCode == 0x0 && excepinfo.scode == S_OK,
        "EXCEPINFO differs from expected: wCode = 0x%x, scode = 0x%08x\n",
        excepinfo.wCode, excepinfo.scode);
    VariantClear(&varresult);
    VariantClear(&vararg[0]);

    /* call Value with a VT_VARIANT|VT_BYREF type */
    V_VT(&vararg[0]) = VT_VARIANT|VT_BYREF;
    V_VARIANTREF(&vararg[0]) = &vararg[1];
    V_VT(&vararg[1]) = VT_I2;
    V_I2(&vararg[1]) = 1;
    dispparams.cNamedArgs = 0;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.cArgs = 1;
    dispparams.rgvarg = vararg;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_VALUE, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_PROPERTYGET, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, IDispatch_Invoke);

    ok(excepinfo.wCode == 0x0 && excepinfo.scode == S_OK,
        "EXCEPINFO differs from expected: wCode = 0x%x, scode = 0x%08x\n",
        excepinfo.wCode, excepinfo.scode);

    ok(V_VT(&varresult) == VT_I2, "V_VT(&varresult) was %d instead of VT_I2\n", V_VT(&varresult));
    ok(V_I2(&varresult) == 1234, "V_I2(&varresult) was %d instead of 1234\n", V_I2(&varresult));
    VariantClear(&varresult);

    /* call Array with BSTR argument - type mismatch */
    VariantInit(&vararg[0]);
    V_VT(&vararg[0]) = VT_BSTR;
    V_BSTR(&vararg[0]) = SysAllocString(szSuperman);
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 1;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = vararg;
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_ARRAY, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
    ok(hr == DISP_E_TYPEMISMATCH || hr == DISP_E_BADVARTYPE, "expected DISP_E_TYPEMISMATCH, got %#x\n", hr);
    SysFreeString(V_BSTR(&vararg[0]));

    /* call ArrayPtr with BSTR argument - type mismatch */
    VariantInit(&vararg[0]);
    V_VT(&vararg[0]) = VT_BSTR;
    V_BSTR(&vararg[0]) = SysAllocString(szSuperman);
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 1;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = vararg;
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_VARARRAYPTR, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
    ok(hr == DISP_E_TYPEMISMATCH || hr == DISP_E_BADVARTYPE, "expected DISP_E_TYPEMISMATCH, got %#x\n", hr);
    SysFreeString(V_BSTR(&vararg[0]));

    /* call VariantCArray - test marshaling of variant arrays */
    V_VT(&vararg[0]) = VT_I4;
    V_I4(&vararg[0]) = 1;
    V_VT(&vararg[1]) = VT_I4;
    V_I4(&vararg[1]) = 2;
    hr = IWidget_VariantCArray(pWidget, 2, vararg);
    ok_ole_success(hr, IWidget_VariantCArray);
todo_wine
    ok(V_VT(&vararg[0]) == VT_I4 && V_I4(&vararg[0]) == 2, "vararg[0] = %d[%d]\n", V_VT(&vararg[0]), V_I4(&vararg[0]));
todo_wine
    ok(V_VT(&vararg[1]) == VT_I4 && V_I4(&vararg[1]) == 3, "vararg[1] = %d[%d]\n", V_VT(&vararg[1]), V_I4(&vararg[1]));

    /* call VarArg */
    VariantInit(&vararg[3]);
    V_VT(&vararg[3]) = VT_I4;
    V_I4(&vararg[3]) = 3;
    VariantInit(&vararg[2]);
    V_VT(&vararg[2]) = VT_I4;
    V_I4(&vararg[2]) = 0;
    VariantInit(&vararg[1]);
    V_VT(&vararg[1]) = VT_I4;
    V_I4(&vararg[1]) = 1;
    VariantInit(&vararg[0]);
    V_VT(&vararg[0]) = VT_I4;
    V_I4(&vararg[0]) = 2;
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 4;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = vararg;
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_VARARG, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
    ok_ole_success(hr, IDispatch_Invoke);

    /* call VarArg, even one (non-optional, non-safearray) named argument is not allowed */
    dispidNamed = 0;
    dispparams.cNamedArgs = 1;
    dispparams.rgdispidNamedArgs = &dispidNamed;
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_VARARG, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
    ok(hr == DISP_E_NONAMEDARGS, "IDispatch_Invoke should have returned DISP_E_NONAMEDARGS instead of 0x%08x\n", hr);
    dispidNamed = DISPID_PROPERTYPUT;

    /* call VarArg_Run */
    VariantInit(&vararg[1]);
    V_VT(&vararg[1]) = VT_BSTR;
    V_BSTR(&vararg[1]) = SysAllocString(szCat);
    VariantInit(&vararg[0]);
    V_VT(&vararg[0]) = VT_BSTR;
    V_BSTR(&vararg[0]) = SysAllocString(szSuperman);
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 2;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = vararg;
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_VARARG_RUN, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
    ok_ole_success(hr, IDispatch_Invoke);
    SysFreeString(V_BSTR(&vararg[1]));
    SysFreeString(V_BSTR(&vararg[0]));

    /* call VarArg_Ref_Run */
    VariantInit(&vararg[1]);
    V_VT(&vararg[1]) = VT_BSTR;
    V_BSTR(&vararg[1]) = SysAllocString(szCat);
    VariantInit(&vararg[0]);
    V_VT(&vararg[0]) = VT_BSTR;
    V_BSTR(&vararg[0]) = SysAllocString(szSuperman);
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 2;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = vararg;
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_VARARG_REF_RUN, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_METHOD, &dispparams, NULL, NULL, NULL);
    ok_ole_success(hr, IDispatch_Invoke);
    SysFreeString(V_BSTR(&vararg[1]));
    SysFreeString(V_BSTR(&vararg[0]));

    /* call Error */
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 0;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = NULL;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_ERROR, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_METHOD, &dispparams, NULL, &excepinfo, NULL);
    ok(hr == DISP_E_EXCEPTION, "IDispatch_Invoke should have returned DISP_E_EXCEPTION instead of 0x%08x\n", hr);
    ok(excepinfo.wCode == 0x0 && excepinfo.scode == E_NOTIMPL,
        "EXCEPINFO differs from expected: wCode = 0x%x, scode = 0x%08x\n",
        excepinfo.wCode, excepinfo.scode);
    VariantClear(&varresult);

    /* call BstrRet */
    pTypeInfo = NonOleAutomation_GetTypeInfo();
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 0;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = NULL;
    VariantInit(&varresult);
    hr = ITypeInfo_Invoke(pTypeInfo, &NonOleAutomation, DISPID_NOA_BSTRRET, DISPATCH_METHOD, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, ITypeInfo_Invoke);
    ok(V_VT(&varresult) == VT_BSTR, "V_VT(&varresult) should be VT_BSTR instead of %d\n", V_VT(&varresult));
    ok(V_BSTR(&varresult) != NULL, "V_BSTR(&varresult) should not be NULL\n");

    VariantClear(&varresult);

    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 0;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = NULL;
    hr = ITypeInfo_Invoke(pTypeInfo, &NonOleAutomation, DISPID_NOA_ERROR, DISPATCH_METHOD, &dispparams, &varresult, &excepinfo, NULL);
    ok(hr == DISP_E_EXCEPTION, "ITypeInfo_Invoke should have returned DISP_E_EXCEPTION instead of 0x%08x\n", hr);
    ok(V_VT(&varresult) == VT_EMPTY, "V_VT(&varresult) should be VT_EMPTY instead of %d\n", V_VT(&varresult));
    ok(excepinfo.wCode == 0x0 && excepinfo.scode == E_NOTIMPL,
        "EXCEPINFO differs from expected: wCode = 0x%x, scode = 0x%08x\n",
        excepinfo.wCode, excepinfo.scode);
    VariantClear(&varresult);

    ITypeInfo_Release(pTypeInfo);

    /* tests call put_Name without named arg */
    VariantInit(&vararg[0]);
    dispparams.cNamedArgs = 0;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.cArgs = 1;
    dispparams.rgvarg = vararg;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_NAME, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_PROPERTYPUT, &dispparams, &varresult, &excepinfo, NULL);
    ok(hr == DISP_E_PARAMNOTFOUND, "IDispatch_Invoke should have returned DISP_E_PARAMNOTFOUND instead of 0x%08x\n", hr);
    VariantClear(&varresult);

    /* tests param type that cannot be coerced */
    VariantInit(&vararg[0]);
    V_VT(&vararg[0]) = VT_UNKNOWN;
    V_UNKNOWN(&vararg[0]) = NULL;
    dispparams.cNamedArgs = 1;
    dispparams.rgdispidNamedArgs = &dispidNamed;
    dispparams.cArgs = 1;
    dispparams.rgvarg = vararg;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_NAME, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_PROPERTYPUT, &dispparams, &varresult, &excepinfo, NULL);
    ok(hr == DISP_E_TYPEMISMATCH, "IDispatch_Invoke should have returned DISP_E_TYPEMISMATCH instead of 0x%08x\n", hr);
    VariantClear(&varresult);

    /* tests bad param type */
    VariantInit(&vararg[0]);
    V_VT(&vararg[0]) = VT_CLSID;
    V_BYREF(&vararg[0]) = NULL;
    dispparams.cNamedArgs = 1;
    dispparams.rgdispidNamedArgs = &dispidNamed;
    dispparams.cArgs = 1;
    dispparams.rgvarg = vararg;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_NAME, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_PROPERTYPUT, &dispparams, &varresult, &excepinfo, NULL);
    ok(hr == DISP_E_BADVARTYPE, "IDispatch_Invoke should have returned DISP_E_BADVARTYPE instead of 0x%08x\n", hr);
    VariantClear(&varresult);

    /* tests too small param count */
    dispparams.cNamedArgs = 0;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.cArgs = 0;
    dispparams.rgvarg = NULL;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_DOSOMETHING, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_METHOD, &dispparams, &varresult, &excepinfo, NULL);
    ok(hr == DISP_E_BADPARAMCOUNT, "IDispatch_Invoke should have returned DISP_E_BADPARAMCOUNT instead of 0x%08x\n", hr);
    VariantClear(&varresult);

    /* tests propget function with large param count */
    VariantInit(&vararg[0]);
    V_VT(&vararg[0]) = VT_BSTR;
    V_BSTR(&vararg[0]) = NULL;
    V_VT(&vararg[1]) = VT_I4;
    V_I4(&vararg[1]) = 1;
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 2;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = vararg;
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_STATE, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_PROPERTYGET, &dispparams, &varresult, &excepinfo, NULL);
    ok(hr == DISP_E_NOTACOLLECTION, "IDispatch_Invoke should have returned DISP_E_NOTACOLLECTION instead of 0x%08x\n", hr);

    /* test propput with lcid */

    /* the lcid passed to the function is the first lcid in the typelib header.
       Since we don't explicitly set an lcid in the idl, it'll default to US English. */
    VariantInit(&vararg[0]);
    V_VT(&vararg[0]) = VT_I4;
    V_I4(&vararg[0]) = 0xcafe;
    dispparams.cNamedArgs = 1;
    dispparams.rgdispidNamedArgs = &dispidNamed;
    dispparams.cArgs = 1;
    dispparams.rgvarg = vararg;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_PROP_WITH_LCID, &IID_NULL, 0x40c, DISPATCH_PROPERTYPUT, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, ITypeInfo_Invoke);
    VariantClear(&varresult);

    /* test propget with lcid */
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 0;
    dispparams.rgvarg = NULL;
    dispparams.rgdispidNamedArgs = NULL;
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_PROP_WITH_LCID, &IID_NULL, 0x40c, DISPATCH_PROPERTYGET, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, ITypeInfo_Invoke);
    ok(V_VT(&varresult) == VT_I4, "got %x\n", V_VT(&varresult));
    ok(V_I4(&varresult) == 0x409, "got %x\n", V_I4(&varresult));
    VariantClear(&varresult);

    /* test propget of INT value */
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 0;
    dispparams.rgvarg = NULL;
    dispparams.rgdispidNamedArgs = NULL;
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_PROP_INT, &IID_NULL, 0x40c, DISPATCH_PROPERTYGET, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, ITypeInfo_Invoke);
    ok(V_VT(&varresult) == VT_I4, "got %x\n", V_VT(&varresult));
    ok(V_I4(&varresult) == -13, "got %x\n", V_I4(&varresult));
    VariantClear(&varresult);

    /* test propget of INT value */
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 0;
    dispparams.rgvarg = NULL;
    dispparams.rgdispidNamedArgs = NULL;
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_PROP_UINT, &IID_NULL, 0x40c, DISPATCH_PROPERTYGET, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, ITypeInfo_Invoke);
    ok(V_VT(&varresult) == VT_UI4, "got %x\n", V_VT(&varresult));
    ok(V_UI4(&varresult) == 42, "got %x\n", V_UI4(&varresult));
    VariantClear(&varresult);

    /* test byref marshalling */
    uval = 666;
    VariantInit(&vararg[0]);
    V_VT(&vararg[0]) = VT_UI4|VT_BYREF;
    V_UI4REF(&vararg[0]) = &uval;
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 1;
    dispparams.rgvarg = vararg;
    dispparams.rgdispidNamedArgs = NULL;
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_BYREF_UINT, &IID_NULL, LOCALE_NEUTRAL, DISPATCH_METHOD, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, ITypeInfo_Invoke);
    ok(V_VT(&varresult) == VT_EMPTY, "varresult should be VT_EMPTY\n");
    ok(V_VT(&vararg[0]) == (VT_UI4|VT_BYREF), "arg VT not unmarshalled correctly: %x\n", V_VT(&vararg[0]));
    ok(V_UI4REF(&vararg[0]) == &uval, "Byref pointer not preserved: %p/%p\n", &uval, V_UI4REF(&vararg[0]));
    ok(*V_UI4REF(&vararg[0]) == 42, "Expected 42 to be returned instead of %u\n", *V_UI4REF(&vararg[0]));
    VariantClear(&varresult);
    VariantClear(&vararg[0]);

    /* test propput with optional argument. */
    VariantInit(&vararg[0]);
    V_VT(&vararg[0]) = VT_I4;
    V_I4(&vararg[0]) = 0xcafe;
    dispparams.cNamedArgs = 1;
    dispparams.rgdispidNamedArgs = &dispidNamed;
    dispparams.cArgs = 1;
    dispparams.rgvarg = vararg;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_PROP_OPT_ARG, &IID_NULL, 0x40c, DISPATCH_PROPERTYPUT, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, ITypeInfo_Invoke);
    VariantClear(&varresult);

    /* test propput with required argument. */
    VariantInit(&vararg[0]);
    VariantInit(&vararg[1]);
    V_VT(&vararg[0]) = VT_I4;
    V_I4(&vararg[0]) = 0x1234;
    V_VT(&vararg[1]) = VT_I4;
    V_I4(&vararg[1]) = 0x5678;
    dispparams.cNamedArgs = 1;
    dispparams.rgdispidNamedArgs = &dispidNamed;
    dispparams.cArgs = 2;
    dispparams.rgvarg = vararg;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_PROP_REQ_ARG, &IID_NULL, 0x40c, DISPATCH_PROPERTYPUT, &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, ITypeInfo_Invoke);
    VariantClear(&varresult);

    /* restricted member */
    dispparams.cNamedArgs = 0;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.cArgs = 0;
    dispparams.rgvarg = NULL;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_RESTRICTED, &IID_NULL, 0x40c, DISPATCH_METHOD, &dispparams, &varresult, &excepinfo, NULL);
    ok( hr == DISP_E_MEMBERNOTFOUND, "got %08x\n", hr );
    VariantClear(&varresult);

    /* restricted member with -ve memid (not restricted) */
    dispparams.cNamedArgs = 0;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.cArgs = 0;
    dispparams.rgvarg = NULL;
    VariantInit(&varresult);
    hr = IDispatch_Invoke(pDispatch, DISPID_TM_NEG_RESTRICTED, &IID_NULL, 0x40c, DISPATCH_METHOD, &dispparams, &varresult, &excepinfo, NULL);
    ok( hr == S_OK, "got %08x\n", hr );
    ok(V_VT(&varresult) == VT_I4, "got %x\n", V_VT(&varresult));
    ok(V_I4(&varresult) == DISPID_TM_NEG_RESTRICTED, "got %x\n", V_I4(&varresult));
    VariantClear(&varresult);

    test_marshal_basetypes(pWidget, pDispatch);
    test_marshal_pointer(pWidget, pDispatch);
    test_marshal_iface(pWidget, pDispatch);
    test_marshal_bstr(pWidget, pDispatch);
    test_marshal_variant(pWidget, pDispatch);

    IDispatch_Release(pDispatch);
    IWidget_Release(pWidget);

    trace("calling end_host_object\n");
    end_host_object(tid, thread);
}

static void test_DispCallFunc(void)
{
    static const WCHAR szEmpty[] = { 0 };
    VARTYPE rgvt[] = { VT_R8, VT_BSTR, VT_BSTR, VT_VARIANT|VT_BYREF };
    VARIANTARG vararg[4];
    VARIANTARG varref;
    VARIANTARG *rgpvarg[4] = { &vararg[0], &vararg[1], &vararg[2], &vararg[3] };
    VARIANTARG varresult;
    HRESULT hr;
    IWidget *pWidget = Widget_Create();
    V_VT(&vararg[0]) = VT_R8;
    V_R8(&vararg[0]) = 3.141;
    V_VT(&vararg[1]) = VT_BSTR;
    V_BSTRREF(&vararg[1]) = CoTaskMemAlloc(sizeof(BSTR));
    V_VT(&vararg[2]) = VT_BSTR;
    V_BSTR(&vararg[2]) = SysAllocString(szEmpty);
    V_VT(&vararg[3]) = VT_VARIANT|VT_BYREF;
    V_VARIANTREF(&vararg[3]) = &varref;
    V_VT(&varref) = VT_ERROR;
    V_ERROR(&varref) = DISP_E_PARAMNOTFOUND;
    VariantInit(&varresult);
    hr = DispCallFunc(pWidget, 9*sizeof(void*), CC_STDCALL, VT_UI4, 4, rgvt, rgpvarg, &varresult);
    ok_ole_success(hr, DispCallFunc);
    VariantClear(&varresult);
    SysFreeString(*V_BSTRREF(&vararg[1]));
    CoTaskMemFree(V_BSTRREF(&vararg[1]));
    VariantClear(&vararg[2]);
    IWidget_Release(pWidget);
}

static void test_StaticWidget(void)
{
    ITypeInfo *type_info;
    DISPPARAMS dispparams;
    VARIANTARG vararg[4];
    EXCEPINFO excepinfo;
    VARIANT varresult;
    HRESULT hr;

    type_info = get_type_info(&IID_IStaticWidget);

    /* call TestDual */
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 1;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = vararg;
    V_VT(vararg) = VT_DISPATCH;
    V_DISPATCH(vararg) = (IDispatch*)&TestDualDisp;
    VariantInit(&varresult);
    hr = ITypeInfo_Invoke(type_info, &StaticWidget, DISPID_TM_TESTDUAL, DISPATCH_METHOD,
            &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, IDispatch_Invoke);
    ok(V_VT(&varresult) == VT_EMPTY, "vt %x\n", V_VT(&varresult));
    VariantClear(&varresult);

    /* call TestSecondIface */
    dispparams.cNamedArgs = 0;
    dispparams.cArgs = 1;
    dispparams.rgdispidNamedArgs = NULL;
    dispparams.rgvarg = vararg;
    V_VT(vararg) = VT_DISPATCH;
    V_DISPATCH(vararg) = (IDispatch*)&TestDualDisp;
    VariantInit(&varresult);
    hr = ITypeInfo_Invoke(type_info, &StaticWidget, DISPID_TM_TESTSECONDIFACE, DISPATCH_METHOD,
            &dispparams, &varresult, &excepinfo, NULL);
    ok_ole_success(hr, IDispatch_Invoke);
    ok(V_VT(&varresult) == VT_EMPTY, "vt %x\n", V_VT(&varresult));
    VariantClear(&varresult);

    ITypeInfo_Release(type_info);
}

static void test_libattr(void)
{
    ITypeLib *pTypeLib;
    HRESULT hr;
    TLIBATTR *pattr;

    hr = LoadRegTypeLib(&LIBID_TestTypelib, 2, 5, LOCALE_NEUTRAL, &pTypeLib);
    ok_ole_success(hr, LoadRegTypeLib);
    if (FAILED(hr))
        return;

    hr = ITypeLib_GetLibAttr(pTypeLib, &pattr);
    ok_ole_success(hr, GetLibAttr);
    if (SUCCEEDED(hr))
    {
        ok(pattr->lcid == MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL), "lcid %x\n", pattr->lcid);

        ITypeLib_ReleaseTLibAttr(pTypeLib, pattr);
    }

    ITypeLib_Release(pTypeLib);
}

static void test_external_connection(void)
{
    IStream *stream, *stream2;
    ITestSecondDisp *second;
    ItestDual *iface;
    HANDLE thread;
    DWORD tid;
    HRESULT hres;

    static const LARGE_INTEGER zero;

    trace("Testing IExternalConnection...\n");

    external_connections = 0;

    /* Marshaling an interface increases external connection count. */
    expect_last_release_closes = FALSE;
    hres = CreateStreamOnHGlobal(NULL, TRUE, &stream);
    ok(hres == S_OK, "CreateStreamOnHGlobal failed: %08x\n", hres);
    tid = start_host_object(stream, &IID_ItestDual, (IUnknown*)&TestDual, MSHLFLAGS_NORMAL, &thread);
    ok(external_connections == 1, "external_connections = %d\n", external_connections);

    IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
    hres = CoUnmarshalInterface(stream, &IID_ItestDual, (void**)&iface);
#ifndef __i386__
    todo_wine
#endif
    ok(hres == S_OK, "CoUnmarshalInterface failed: %08x\n", hres);
    if (FAILED(hres))
    {
        end_host_object(tid, thread);
        IStream_Release(stream);
        return;
    }
    ok(external_connections == 1, "external_connections = %d\n", external_connections);

    IStream_Release(stream);
    ok(external_connections == 1, "external_connections = %d\n", external_connections);

    /* Creating a stub for new iface causes new external connection. */
    hres = ItestDual_QueryInterface(iface, &IID_ITestSecondDisp, (void**)&second);
    ok(hres == S_OK, "Could not get ITestSecondDisp iface: %08x\n", hres);
    todo_wine
    ok(external_connections == 2, "external_connections = %d\n", external_connections);

    ITestSecondDisp_Release(second);
    todo_wine
    ok(external_connections == 2, "external_connections = %d\n", external_connections);

    expect_last_release_closes = TRUE;
    ItestDual_Release(iface);
    ok(external_connections == 0, "external_connections = %d\n", external_connections);

    end_host_object(tid, thread);

    /* A test with direct CoMarshalInterface call. */
    hres = CreateStreamOnHGlobal(NULL, TRUE, &stream);
    ok(hres == S_OK, "CreateStreamOnHGlobal failed: %08x\n", hres);

    expect_last_release_closes = FALSE;
    hres = CoMarshalInterface(stream, &IID_ItestDual, (IUnknown*)&TestDual, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL);
    ok(hres == S_OK, "CoMarshalInterface failed: %08x\n", hres);
    ok(external_connections == 1, "external_connections = %d\n", external_connections);

    expect_last_release_closes = TRUE;
    IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
    hres = CoReleaseMarshalData(stream);
    ok(hres == S_OK, "CoReleaseMarshalData failed: %08x\n", hres);
    ok(external_connections == 0, "external_connections = %d\n", external_connections);

    /* Two separated marshal data are still one external connection. */
    hres = CreateStreamOnHGlobal(NULL, TRUE, &stream2);
    ok(hres == S_OK, "CreateStreamOnHGlobal failed: %08x\n", hres);

    expect_last_release_closes = FALSE;
    IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
    hres = CoMarshalInterface(stream, &IID_ItestDual, (IUnknown*)&TestDual, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL);
    ok(hres == S_OK, "CoMarshalInterface failed: %08x\n", hres);
    ok(external_connections == 1, "external_connections = %d\n", external_connections);

    hres = CoMarshalInterface(stream2, &IID_ItestDual, (IUnknown*)&TestDual, MSHCTX_INPROC, NULL, MSHLFLAGS_NORMAL);
    ok(hres == S_OK, "CoMarshalInterface failed: %08x\n", hres);
    ok(external_connections == 1, "external_connections = %d\n", external_connections);

    IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
    hres = CoReleaseMarshalData(stream);
    ok(hres == S_OK, "CoReleaseMarshalData failed: %08x\n", hres);
    ok(external_connections == 1, "external_connections = %d\n", external_connections);

    expect_last_release_closes = TRUE;
    IStream_Seek(stream2, zero, STREAM_SEEK_SET, NULL);
    hres = CoReleaseMarshalData(stream2);
    ok(hres == S_OK, "CoReleaseMarshalData failed: %08x\n", hres);
    ok(external_connections == 0, "external_connections = %d\n", external_connections);

    IStream_Release(stream);
    IStream_Release(stream2);

    /* Weak table marshaling does not increment external connections */
    hres = CreateStreamOnHGlobal(NULL, TRUE, &stream);
    ok(hres == S_OK, "CreateStreamOnHGlobal failed: %08x\n", hres);

    hres = CoMarshalInterface(stream, &IID_ItestDual, (IUnknown*)&TestDual, MSHCTX_INPROC, NULL, MSHLFLAGS_TABLEWEAK);
    ok(hres == S_OK, "CoMarshalInterface failed: %08x\n", hres);
    ok(external_connections == 0, "external_connections = %d\n", external_connections);

    IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
    hres = CoUnmarshalInterface(stream, &IID_ItestDual, (void**)&iface);
    ok(hres == S_OK, "CoUnmarshalInterface failed: %08x\n", hres);
    ok(external_connections == 0, "external_connections = %d\n", external_connections);
    ItestDual_Release(iface);

    IStream_Seek(stream, zero, STREAM_SEEK_SET, NULL);
    hres = CoReleaseMarshalData(stream);
    ok(hres == S_OK, "CoReleaseMarshalData failed: %08x\n", hres);
    ok(external_connections == 0, "external_connections = %d\n", external_connections);

    IStream_Release(stream);
}

START_TEST(tmarshal)
{
    HRESULT hr;
    HANDLE hOleaut32 = GetModuleHandleA("oleaut32.dll");
    pVarAdd = (void*)GetProcAddress(hOleaut32, "VarAdd");

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    hr = register_current_module_typelib();
    if (FAILED(hr))
    {
        CoUninitialize();
        win_skip("Registration of the test typelib failed, skipping tests\n");
        return;
    }

    test_typelibmarshal();
    test_DispCallFunc();
    test_StaticWidget();
    test_libattr();
    test_external_connection();

    hr = UnRegisterTypeLib(&LIBID_TestTypelib, 2, 5, LOCALE_NEUTRAL,
                           sizeof(void*) == 8 ? SYS_WIN64 : SYS_WIN32);
    ok_ole_success(hr, UnRegisterTypeLib);

    CoUninitialize();
}
