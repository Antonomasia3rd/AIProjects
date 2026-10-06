// ==WindhawkMod==
// @id              dwm-3d-experiment
// @name            DWM 3D Transforms
// @description     Restores Windows Vista-7-8.1 3D animations using DWM internal visuals
// @version         0.3
// @author          xalejandro
// @github          https://github.com/tetawaves
// @include         dwm.exe
// @compilerOptions -ld2d1 -lwevtapi -lntdll
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# DWM 3D Transforms
Restores Windows Vista-7-8.1 3D animations using DWM internal visuals.

Keep in mind that this mod doesn't fix any of the quirks that the DWM animation system has.
Before reporting an issue make sure this is not the case.

## Supported Windows versions
- Windows 10 2004-22H2
- Windows 11 21H2-25H2

## References
- [OpenGlass](https://github.com/ALTaleX531/OpenGlass)
- 3D Aero Transforms Windhawk mod from [kieldbg](https://github.com/kieldbg)
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- windows81: false
  $name: Windows 8.1 Minimize/Restore Animation
- scaleshrink: true
  $name: Windows 7 Scale Shrink
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>
#include <d2d1_1.h>
#include <d3d9types.h>
#include <winevt.h>
#include <uxtheme.h>

#include <cmath>
#include <unordered_map>

EXTERN_C NTSYSAPI NTSTATUS NTAPI RtlGetVersion(PRTL_OSVERSIONINFOW lpVersionInformation);

// Bring D2D1 stuff that is not included in MinGW headers
#ifdef __cplusplus
extern "C"
{
#endif

    void WINAPI
    D2D1SinCos(
      _In_ FLOAT angle,
      _Out_ FLOAT *s,
      _Out_ FLOAT *c
      );
           
    FLOAT WINAPI
    D2D1Tan(
        _In_ FLOAT angle
        );
        
    FLOAT WINAPI
    D2D1Vec3Length(
      _In_ FLOAT x,
      _In_ FLOAT y,
      _In_ FLOAT z
      );
        
#ifdef __cplusplus
}
#endif

namespace D2D1
{
    class Matrix4x4F : public D2D1_MATRIX_4X4_F
    {
    public:

        COM_DECLSPEC_NOTHROW
        inline
        Matrix4x4F(
            FLOAT m11, FLOAT m12, FLOAT m13, FLOAT m14,
            FLOAT m21, FLOAT m22, FLOAT m23, FLOAT m24,
            FLOAT m31, FLOAT m32, FLOAT m33, FLOAT m34,
            FLOAT m41, FLOAT m42, FLOAT m43, FLOAT m44
            )
        {
            _11 = m11;
            _12 = m12;
            _13 = m13;
            _14 = m14;

            _21 = m21;
            _22 = m22;
            _23 = m23;
            _24 = m24;

            _31 = m31;
            _32 = m32;
            _33 = m33;
            _34 = m34;

            _41 = m41;
            _42 = m42;
            _43 = m43;
            _44 = m44;
        }

        COM_DECLSPEC_NOTHROW
        inline
        Matrix4x4F()
        {
            _11 = 1;
            _12 = 0;
            _13 = 0;
            _14 = 0;

            _21 = 0;
            _22 = 1;
            _23 = 0;
            _24 = 0;

            _31 = 0;
            _32 = 0;
            _33 = 1;
            _34 = 0;

            _41 = 0;
            _42 = 0;
            _43 = 0;
            _44 = 1;
        }

        COM_DECLSPEC_NOTHROW
        inline
        bool
        operator==(
            const Matrix4x4F& r
            ) const
        {
            return _11 == r._11 && _12 == r._12 && _13 == r._13 && _14 == r._14 &&
                   _21 == r._21 && _22 == r._22 && _23 == r._23 && _24 == r._24 &&
                   _31 == r._31 && _32 == r._32 && _33 == r._33 && _34 == r._34 &&
                   _41 == r._41 && _42 == r._42 && _43 == r._43 && _44 == r._44;
        }

        COM_DECLSPEC_NOTHROW
        inline
        bool
        operator!=(
            const Matrix4x4F& r
            ) const
        {
            return !(*this == r);
        }

        static
        COM_DECLSPEC_NOTHROW
        inline
        Matrix4x4F
        Translation(FLOAT x, FLOAT y, FLOAT z)
        {
            Matrix4x4F translation;

            translation._11 = 1.0; translation._12 = 0.0; translation._13 = 0.0; translation._14 = 0.0;
            translation._21 = 0.0; translation._22 = 1.0; translation._23 = 0.0; translation._24 = 0.0;
            translation._31 = 0.0; translation._32 = 0.0; translation._33 = 1.0; translation._34 = 0.0;
            translation._41 = x;   translation._42 = y;   translation._43 = z;   translation._44 = 1.0;

            return translation;
        }

        static
        COM_DECLSPEC_NOTHROW
        inline
        Matrix4x4F
        Scale(FLOAT x, FLOAT y, FLOAT z)
        {
            Matrix4x4F scale;

            scale._11 = x;   scale._12 = 0.0; scale._13 = 0.0; scale._14 = 0.0;
            scale._21 = 0.0; scale._22 = y;   scale._23 = 0.0; scale._24 = 0.0;
            scale._31 = 0.0; scale._32 = 0.0; scale._33 = z;   scale._34 = 0.0;
            scale._41 = 0.0; scale._42 = 0.0; scale._43 = 0.0; scale._44 = 1.0;

            return scale;
        }

        static
        COM_DECLSPEC_NOTHROW
        inline
        Matrix4x4F
        RotationX(FLOAT degreeX)
        {
            FLOAT angleInRadian = degreeX * (3.141592654f / 180.0f);

            FLOAT sinAngle = 0.0;
            FLOAT cosAngle = 0.0;
            D2D1SinCos(angleInRadian, &sinAngle, &cosAngle);

            Matrix4x4F rotationX(
                1, 0,         0,        0,
                0, cosAngle,  sinAngle, 0,
                0, -sinAngle, cosAngle, 0,
                0, 0,         0,        1
                );

            return rotationX;
        }

        static
        COM_DECLSPEC_NOTHROW
        inline
        Matrix4x4F
        RotationY(FLOAT degreeY)
        {
            FLOAT angleInRadian = degreeY * (3.141592654f / 180.0f);

            FLOAT sinAngle = 0.0;
            FLOAT cosAngle = 0.0;
            D2D1SinCos(angleInRadian, &sinAngle, &cosAngle);

            Matrix4x4F rotationY(
                cosAngle, 0, -sinAngle, 0,
                0,        1, 0,         0,
                sinAngle, 0, cosAngle,  0,
                0,        0, 0,         1
                );

            return rotationY;
        }

        static
        COM_DECLSPEC_NOTHROW
        inline
        Matrix4x4F
        RotationZ(FLOAT degreeZ)
        {
            FLOAT angleInRadian = degreeZ * (3.141592654f / 180.0f);

            FLOAT sinAngle = 0.0;
            FLOAT cosAngle = 0.0;
            D2D1SinCos(angleInRadian, &sinAngle, &cosAngle);

            Matrix4x4F rotationZ(
                cosAngle,  sinAngle, 0, 0,
                -sinAngle, cosAngle, 0, 0,
                0,         0,        1, 0,
                0,         0,        0, 1
                );

            return rotationZ;
        }

        //
        // 3D Rotation matrix for an arbitrary axis specified by x, y and z
        //
        static
        COM_DECLSPEC_NOTHROW
        inline
        Matrix4x4F
        RotationArbitraryAxis(FLOAT x, FLOAT y, FLOAT z, FLOAT degree)
        {
            // Normalize the vector represented by x, y, and z
            FLOAT magnitude = D2D1Vec3Length(x, y, z);
            x /= magnitude;
            y /= magnitude;
            z /= magnitude;

            FLOAT angleInRadian = degree * (3.141592654f / 180.0f);

            FLOAT sinAngle = 0.0;
            FLOAT cosAngle = 0.0;
            D2D1SinCos(angleInRadian, &sinAngle, &cosAngle);

            FLOAT oneMinusCosAngle = 1 - cosAngle;

            Matrix4x4F rotationArb(
                1             + oneMinusCosAngle * (x * x - 1),
                z  * sinAngle + oneMinusCosAngle *  x * y,
                -y * sinAngle + oneMinusCosAngle *  x * z,
                0,

                -z * sinAngle + oneMinusCosAngle *  y * x,
                1             + oneMinusCosAngle * (y * y - 1),
                x  * sinAngle + oneMinusCosAngle *  y * z,
                0,

                y  * sinAngle + oneMinusCosAngle *  z * x,
                -x * sinAngle + oneMinusCosAngle *  z * y,
                1             + oneMinusCosAngle * (z * z - 1) ,
                0,

                0, 0, 0, 1
                );

            return rotationArb;
        }

        static
        COM_DECLSPEC_NOTHROW
        inline
        Matrix4x4F
        SkewX(FLOAT degreeX)
        {
            FLOAT angleInRadian = degreeX * (3.141592654f / 180.0f);

            FLOAT tanAngle = D2D1Tan(angleInRadian);

            Matrix4x4F skewX(
                1,          0,  0, 0,
                tanAngle,   1,  0, 0,
                0,          0,  1, 0,
                0,          0,  0, 1
                );

            return skewX;
        }

        static
        COM_DECLSPEC_NOTHROW
        inline
        Matrix4x4F
        SkewY(FLOAT degreeY)
        {
            FLOAT angleInRadian = degreeY * (3.141592654f / 180.0f);

            FLOAT tanAngle = D2D1Tan(angleInRadian);

            Matrix4x4F skewY(
                1,  tanAngle,   0, 0,
                0,  1,          0, 0,
                0,  0,          1, 0,
                0,  0,          0, 1
                );

            return skewY;
        }


        static
        COM_DECLSPEC_NOTHROW
        inline
        Matrix4x4F
        PerspectiveProjection(FLOAT depth)
        {
            float proj = 0;

            if (depth > 0)
            {
                proj = -1/depth;
            }

            Matrix4x4F projection(
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, proj,
                0, 0, 0, 1
                );

            return projection;
        }

        //
        // Functions for convertion from the base D2D1_MATRIX_4X4_f to
        // this type without making a copy
        //
        static
        COM_DECLSPEC_NOTHROW
        inline
        const Matrix4x4F*
        ReinterpretBaseType(const D2D1_MATRIX_4X4_F *pMatrix)
        {
            return static_cast<const Matrix4x4F *>(pMatrix);
        }

        static
        COM_DECLSPEC_NOTHROW
        inline
        Matrix4x4F*
        ReinterpretBaseType(D2D1_MATRIX_4X4_F *pMatrix)
        {
            return static_cast<Matrix4x4F *>(pMatrix);
        }

        COM_DECLSPEC_NOTHROW
        inline
        FLOAT
        Determinant() const
        {
            FLOAT minor1 = _41 * (_12 * (_23 * _34 - _33 * _24) - _13 * (_22 * _34 - _24 * _32) + _14 * (_22 * _33 - _23 * _32));
            FLOAT minor2 = _42 * (_11 * (_21 * _34 - _31 * _24) - _13 * (_21 * _34 - _24 * _31) + _14 * (_21 * _33 - _23 * _31));
            FLOAT minor3 = _43 * (_11 * (_22 * _34 - _32 * _24) - _12 * (_21 * _34 - _24 * _31) + _14 * (_21 * _32 - _22 * _31));
            FLOAT minor4 = _44 * (_11 * (_22 * _33 - _32 * _23) - _12 * (_21 * _33 - _23 * _31) + _13 * (_21 * _32 - _22 * _31));

            return minor1 - minor2 + minor3 - minor4;
        }

        COM_DECLSPEC_NOTHROW
        inline
        bool
        IsIdentity() const
        {
            return _11 == 1.f && _12 == 0.f && _13 == 0.f && _14 == 0.f
                && _21 == 0.f && _22 == 1.f && _23 == 0.f && _24 == 0.f
                && _31 == 0.f && _32 == 0.f && _33 == 1.f && _34 == 0.f
                && _41 == 0.f && _42 == 0.f && _43 == 0.f && _44 == 1.f;
        }

        COM_DECLSPEC_NOTHROW
        inline
        void
        SetProduct(const Matrix4x4F &a, const Matrix4x4F &b)
        {
            _11 = a._11 * b._11 + a._12 * b._21 + a._13 * b._31 + a._14 * b._41;
            _12 = a._11 * b._12 + a._12 * b._22 + a._13 * b._32 + a._14 * b._42;
            _13 = a._11 * b._13 + a._12 * b._23 + a._13 * b._33 + a._14 * b._43;
            _14 = a._11 * b._14 + a._12 * b._24 + a._13 * b._34 + a._14 * b._44;

            _21 = a._21 * b._11 + a._22 * b._21 + a._23 * b._31 + a._24 * b._41;
            _22 = a._21 * b._12 + a._22 * b._22 + a._23 * b._32 + a._24 * b._42;
            _23 = a._21 * b._13 + a._22 * b._23 + a._23 * b._33 + a._24 * b._43;
            _24 = a._21 * b._14 + a._22 * b._24 + a._23 * b._34 + a._24 * b._44;
 
            _31 = a._31 * b._11 + a._32 * b._21 + a._33 * b._31 + a._34 * b._41;
            _32 = a._31 * b._12 + a._32 * b._22 + a._33 * b._32 + a._34 * b._42;
            _33 = a._31 * b._13 + a._32 * b._23 + a._33 * b._33 + a._34 * b._43;
            _34 = a._31 * b._14 + a._32 * b._24 + a._33 * b._34 + a._34 * b._44;

            _41 = a._41 * b._11 + a._42 * b._21 + a._43 * b._31 + a._44 * b._41;
            _42 = a._41 * b._12 + a._42 * b._22 + a._43 * b._32 + a._44 * b._42;
            _43 = a._41 * b._13 + a._42 * b._23 + a._43 * b._33 + a._44 * b._43;
            _44 = a._41 * b._14 + a._42 * b._24 + a._43 * b._34 + a._44 * b._44;
        }

        COM_DECLSPEC_NOTHROW
        inline
        Matrix4x4F
        operator*(const Matrix4x4F &matrix) const
        {
            Matrix4x4F result;

            result.SetProduct(*this, matrix);

            return result;
        }
    };
}

enum class WindowAnimationType
{
    AnimationNone = 0,
    AnimationOpen = 1,
    AnimationMaximize = 2,
    AnimationMinimize = 3,
    AnimationRestoreFromMaximized = 4,
    AnimationRestoreFromMinimized = 5,
    AnimationClose = 6,
};

enum os_build : ULONG
{
    build_w10_1507 = 10240,
    build_w10_1511 = 10586,
    build_w10_1607 = 14393,
    build_w10_1703 = 15063,
    build_w10_1709 = 16299,
    build_w10_1803 = 17134,
    build_w10_1809 = 17763,
    build_w10_1903 = 18362,
    build_w10_1909 = 18363,
    build_w10_2004 = 19041,
    build_w10_20h2 = 19042,
    build_w10_21h1 = 19043,
    build_w10_21h2 = 19044,
    build_w10_22h2 = 19045,
    build_w11_21h2 = 22000,
    build_w11_22h2 = 22621,
    build_w11_23h2 = 22631,
    build_w11_24h2 = 26100,
    build_w11_25h2 = 26200,
};

ULONG g_buildNumber = 0;
bool g_fWindows81 = false;
bool g_fScaleShrink = false;

struct CBaseObject
{
    size_t AddRef()
    {
        return InterlockedIncrement(reinterpret_cast<DWORD*>(this) + 2);
    }
    size_t Release()
    {
        auto result = InterlockedDecrement(reinterpret_cast<DWORD*>(this) + 2);
        if (!result)
        {
            delete this;
        }
        return result;
    }
    HRESULT QueryInterface(
        [[maybe_unused]] REFIID riid, 
        [[maybe_unused]] PVOID* ppvObject
    )
    {
        return E_NOTIMPL;
    }
    protected: virtual ~CBaseObject() {}
};

struct CMatrixTransform3dProxy : CBaseObject{};
struct CEffectGroupProxy : CBaseObject{};
struct CVisualProxy;

struct CVisual
{
    CVisualProxy *GetVisualProxy() const
    {
        return reinterpret_cast<CVisualProxy* const*>(this)[2];
    }
};
struct CTimeline
{
    double &GetDurationValue()
    {
        return *&(reinterpret_cast<double*>(this)[3]);
    }
    double &GetBeginValue()
    {
        return *&(reinterpret_cast<double*>(this)[4]);
    }
    double &GetEndValue()
    {
        return *&(reinterpret_cast<double*>(this)[5]);
    }
    double &GetCurrentValue()
    {
        return *&(reinterpret_cast<double*>(this)[6]);
    }
};
struct CWindowData
{
    const RECT *GetWindowRect()
    {
       return reinterpret_cast<const RECT *>(this + 48);
    }
};
struct CTopLevelWindow3D : CVisual
{
    WindowAnimationType GetCurrentAnimationType() const
    {
        WindowAnimationType animationType = WindowAnimationType::AnimationNone;
        if (g_buildNumber < os_build::build_w11_21h2)
        {
            animationType = reinterpret_cast<WindowAnimationType const*>(this)[92];
        }
        else if (g_buildNumber < os_build::build_w11_24h2)
        {
            animationType = reinterpret_cast<WindowAnimationType const*>(this)[96];
        }
        else
        {
            animationType = reinterpret_cast<WindowAnimationType const*>(this)[84];            
        }
        return animationType;
    }
    CWindowData *GetData() const
	{
        CWindowData *data = nullptr;
        if (g_buildNumber < os_build::build_w11_21h2)
        {
            data = reinterpret_cast<CWindowData* const*>(this)[41];
        }
        else if (g_buildNumber < os_build::build_w11_24h2)
        {
            data = reinterpret_cast<CWindowData* const*>(this)[42];
        }
        else
        {
            data = reinterpret_cast<CWindowData* const*>(this)[36];           
        }
        return data;
    }
    const RECT *GetMinimizeRect() const
    {
        const RECT *minimizeRect = nullptr;
        if (g_buildNumber < os_build::build_w11_21h2)
        {
            minimizeRect = reinterpret_cast<const RECT *>(this + 292);
        }
        else if (g_buildNumber < os_build::build_w11_22h2)
        {
            minimizeRect = reinterpret_cast<const RECT *>(this + 300);
        }
        else if (g_buildNumber < os_build::build_w11_24h2)
        {
            minimizeRect = reinterpret_cast<const RECT *>(this + 304);
        }
        else
        {
            minimizeRect = reinterpret_cast<const RECT *>(this + 256);
        }
        return minimizeRect;
    }
    CTimeline* GetAnimationTimeline() const
    {
        CTimeline *timeline = nullptr;
        if (g_buildNumber < os_build::build_w11_21h2)
        {
            timeline = reinterpret_cast<CTimeline* const*>(this)[45];
        }
        else if (g_buildNumber < os_build::build_w11_24h2)
        {
            timeline = reinterpret_cast<CTimeline* const*>(this)[46];
        }
        else
        {
            timeline = reinterpret_cast<CTimeline* const*>(this)[40];
        }
        return timeline;
    }
};
struct CAnimatedTransitionVisual
{
    CVisual *GetVisual()
    {
        return reinterpret_cast<CVisual *>(this + 8);
    }
    int GetStoryboardId() const
    {
        int storyboardId = 0;
        if (g_buildNumber < os_build::build_w11_21h2)
        {
            storyboardId = reinterpret_cast<int const*>(this)[182];
        }
        else if (g_buildNumber < os_build::build_w11_24h2)
        {
            storyboardId = reinterpret_cast<int const*>(this)[184];   
        }
        else
        {
            storyboardId = reinterpret_cast<int const*>(this)[172]; 
        }
        return storyboardId;
    }
    const RECT *GetBeginRect() const
    {
        const RECT *beginRect = nullptr;
        if (g_buildNumber < os_build::build_w11_21h2)
        {
            beginRect = reinterpret_cast<const RECT *>(this + 848);
        }
        else if (g_buildNumber < os_build::build_w11_24h2)
        {
            beginRect = reinterpret_cast<const RECT *>(this + 856);
        }
        else
        {
            beginRect = reinterpret_cast<const RECT *>(this + 808);
        }
        return beginRect;
    }
};
struct CDesktopManager
{
    inline static CDesktopManager** s_pDesktopManagerInstance{ nullptr };

    static CDesktopManager* GetInstance()
    {
        return *s_pDesktopManagerInstance;
    }
    void* GetCompositor() const
    {
        void *compositor = nullptr;
        if (g_buildNumber < os_build::build_w11_22h2)
        {
            compositor = reinterpret_cast<void* const*>(this)[5];
        }
        else
        {
            compositor = reinterpret_cast<void* const*>(this)[6];
        }
        return compositor;
    }
    double GetAnimationTime() const
    {
        double animationTime = 0.f;
        if (g_buildNumber < os_build::build_w11_21h2)
        {
            animationTime = reinterpret_cast<double const*>(this)[40];
        }
        else if (g_buildNumber < os_build::build_w11_22h2)
        {
            animationTime = reinterpret_cast<double const*>(this)[31];
        }
        else if (g_buildNumber < os_build::build_w11_24h2)
        {
            animationTime = reinterpret_cast<double const*>(this)[33];
        }
        else
        {
            animationTime = reinterpret_cast<double const*>(this)[32];
        }
        return animationTime;
    }
};

struct AnimationData 
{
    CMatrixTransform3dProxy *matrix;
    CEffectGroupProxy *effect;
    CEffectGroupProxy *emptyEffect;
    WindowAnimationType currentAnimationType;
    RECT previousRect;
};

struct TransitionAnimationData
{
    CMatrixTransform3dProxy *matrix;
    CEffectGroupProxy *effect;
    double startTime;
};

std::unordered_map<CTopLevelWindow3D*, AnimationData> g_windowMap{};
std::unordered_map<CAnimatedTransitionVisual*, TransitionAnimationData> g_transitionVisualMap{};

LPCWSTR g_kNoInjectDwmMsg =
    L"DWM was unexpectedly restarted. The mod will not be reinjected to avoid a potential "
    L"crash loop. Please disable and re-enable the mod to force reinjection.";

HRESULT (*CCompositor_CreateProxy_CMatrixTransform3dProxy)(void *, CMatrixTransform3dProxy **); 
HRESULT (*CCompositor_CreateProxy_CEffectGroupProxy)(void *, CEffectGroupProxy **);
HRESULT (*CMatrixTransform3dProxy_Update)(CMatrixTransform3dProxy *, D3DMATRIX *);
HRESULT (*CEffectGroupProxy_Update)(void *, double, void *, void *);
HRESULT (*CVisualProxy_SetEffect)(void *, void *);
void (*CVisual_SetDirtyFlags)(CVisual *, DWORD);

bool HasDwminitWarningRecently()
{
    const WCHAR* queryPath = L"Application";
    const WCHAR* query =
        L"*[System[Provider[@Name='Dwminit'] and (Level=3) and "
        L"TimeCreated[timediff(@SystemTime) <= 10000]]]";

    EVT_HANDLE queryHandle = EvtQuery(nullptr,    // Local machine
                                      queryPath,  // Log path (Application log)
                                      query,      // Query
                                      EvtQueryChannelPath  // Query flags
    );

    if (!queryHandle)
    {
        Wh_Log(L"EvtQuery failed with error: %u", GetLastError());
        return false;
    }

    bool found = false;

    EVT_HANDLE eventHandle = nullptr;
    DWORD dwReturned = 0;
    constexpr DWORD kTimeout = 1000;

    if (EvtNext(queryHandle, 1, &eventHandle, kTimeout, 0, &dwReturned))
    {
        found = true;
        EvtClose(eventHandle);
    }
    else if (GetLastError() != ERROR_NO_MORE_ITEMS)
    {
        Wh_Log(L"EvtNext failed with error: %u", GetLastError());
    }

    EvtClose(queryHandle);
    return found;
}

D2D1::Matrix4x4F MakePerspective()
{
    float depth = 1.01f;
    auto projection = D2D1::Matrix4x4F::PerspectiveProjection(depth);

    if (g_fScaleShrink)
        return D2D1::Matrix4x4F::Scale(1.f / depth, 1.f / depth, 1.f) * projection;

    return projection;
}

D2D1::Matrix4x4F BuildMatrix(float *opacity, WindowAnimationType animationType, float timelineProgress, RECT rc, RECT rcMinimize)
{
    float width = static_cast<float>(rc.right) - static_cast<float>(rc.left);
    float height = static_cast<float>(rc.bottom)- static_cast<float>(rc.top);

    D2D1::Matrix4x4F matrixModel = D2D1::Matrix4x4F();
    D2D1::Matrix4x4F matrixCamera = D2D1::Matrix4x4F();
    D2D1::Matrix4x4F matrixOffset = D2D1::Matrix4x4F();

    if (animationType == WindowAnimationType::AnimationOpen || animationType == WindowAnimationType::AnimationClose)
    {
        bool open = animationType == WindowAnimationType::AnimationOpen;

        float ease = open ? sqrtf(timelineProgress) : 1.f - sqrtf(1.0f - timelineProgress);
        float invEase = 1.f - ease;

        float pitch = open ? invEase * 5.f : ease * -5.f;
        float yaw = open ? 0.f : ease * -2.f;

        float pivotY = open ? .5f : 1.f;
        float zTrans = open ? invEase * .1f : 0.f;
        float yTrans = open ? invEase * -.017f : 0.f;
        
        *opacity = open ? ease : 1.f - timelineProgress;
        
        matrixModel =
        {
            D2D1::Matrix4x4F::Scale(1.f / width, 1.f / height, 1.f) *
            D2D1::Matrix4x4F::Translation(0.f, -pivotY, 0.f) *
            D2D1::Matrix4x4F::RotationX(pitch) *
            D2D1::Matrix4x4F::RotationY(yaw) *
            D2D1::Matrix4x4F::Translation(0.f, pivotY + yTrans, zTrans) *
            D2D1::Matrix4x4F::Scale(width, height, 1.f)
        };
        matrixCamera =
        {
            D2D1::Matrix4x4F::Translation(-width / 2.f, -height / 2.f, .0f) *
            D2D1::Matrix4x4F::Scale(1.f, 1.f, -1.f) * 
            MakePerspective() *
            D2D1::Matrix4x4F::Translation(width / 2.f, height / 2.f, 0.f)
        };
    }
    else if (animationType == WindowAnimationType::AnimationMinimize || animationType == WindowAnimationType::AnimationRestoreFromMinimized)
    {
        bool minimize = animationType == WindowAnimationType::AnimationMinimize;
        float cv = minimize ? 1.f - timelineProgress : 1.f - sqrtf(1.f - timelineProgress);
        float v1 = minimize ? timelineProgress : 1.f - cv;

        float finalW = static_cast<float>(rcMinimize.right  - rcMinimize.left) * .6f;
        float finalH = finalW * (height / width);
        float finalX = static_cast<float>(rcMinimize.left) + (finalW * .3f);
        float finalY = static_cast<float>(rcMinimize.top) - finalH;

        float curW = (1.f - v1) * width + v1 * finalW;
        float curH = (1.f - v1) * height + v1 * finalH;
        float curX = (1.f - v1) * static_cast<float>(rc.left) + v1 * finalX;
        float curY = (1.f - v1) * static_cast<float>(rc.top) + v1 * finalY;

        *opacity = minimize ? cv * .85f : cv;

        float pitch = g_fWindows81 ? 0.f : v1 * -5.f;
        float yaw = g_fWindows81 ? 0.f : v1 * -8.f;
        float roll = g_fWindows81 ? 0.f : v1 * -1.f;

        matrixModel =
        {
            D2D1::Matrix4x4F::Scale(1.f / width, 1.f / height, 1.f) *
            D2D1::Matrix4x4F::Translation(0.f, -1.f, 0.f) *
            D2D1::Matrix4x4F::RotationZ(roll) *
            D2D1::Matrix4x4F::RotationX(pitch) *
            D2D1::Matrix4x4F::RotationY(yaw) *
            D2D1::Matrix4x4F::Translation(0.f, 1.f, 0.f) *
            D2D1::Matrix4x4F::Scale(curW, curH, 1.f)
        };

        float camX = width / 2.0f, camY = height / 2.0f;

        matrixCamera =
        {
            D2D1::Matrix4x4F::Translation(-camX, -camY, 0.f) *
            D2D1::Matrix4x4F::Scale(1.f, 1.f, -1.f) *
            MakePerspective() *
            D2D1::Matrix4x4F::Translation(camX, camY, 0.f)
        };

        matrixOffset = D2D1::Matrix4x4F::Translation(curX - static_cast<float>(rc.left), curY - static_cast<float>(rc.top), 0.f);
    }

    return
    {
        D2D1::Matrix4x4F::Translation(-static_cast<float>(rc.left), -static_cast<float>(rc.top), 0.f) *
        matrixModel *
        matrixCamera *
        matrixOffset *
        D2D1::Matrix4x4F::Translation(static_cast<float>(rc.left), static_cast<float>(rc.top), 0.f)
    };
}

void UpdateTransitionVisual(CAnimatedTransitionVisual *pThis, TransitionAnimationData& animData)
{
    if (!animData.matrix || !animData.effect)
    {
        return;
    }

    auto windowRect = *pThis->GetBeginRect();
    double animationTime = CDesktopManager::GetInstance()->GetAnimationTime();
    if (animData.startTime < 0.f)
        animData.startTime = animationTime;
    double progress = (animationTime - animData.startTime) / .166f;
    float timelineProgress = static_cast<float>(std::max(0.0, std::min(1.0, progress)));
    float opacity = 0.f;
    RECT rc = {};
    auto matrix = BuildMatrix(&opacity, WindowAnimationType::AnimationClose, timelineProgress, windowRect, rc);

    if (HRESULT hr = CMatrixTransform3dProxy_Update(animData.matrix, reinterpret_cast<D3DMATRIX*>(&matrix)); FAILED(hr))
    {
        Wh_Log(L"Failed to update matrix transform 3d proxy: %x", hr);
        return;
    }

    if (HRESULT hr = CEffectGroupProxy_Update(animData.effect, static_cast<double>(opacity), 0, animData.matrix); FAILED(hr))
    {
        Wh_Log(L"Failed to update effect group proxy: %x", hr);
        return;
    }

    auto visualProxy = pThis->GetVisual()->GetVisualProxy();

    if (HRESULT hr = CVisualProxy_SetEffect(visualProxy, animData.effect); FAILED(hr))
    {
        Wh_Log(L"Failed to set effect to visual proxy: %x", hr);
        return;
    }

}

HRESULT (*CTopLevelWindow3D_StartAnimation_orig)(CTopLevelWindow3D *pThis, WindowAnimationType);
HRESULT CTopLevelWindow3D_StartAnimation_hook(CTopLevelWindow3D *pThis, WindowAnimationType currentAnimationType)
{
    auto previousRect = *pThis->GetData()->GetWindowRect();
    auto compositor = CDesktopManager::GetInstance()->GetCompositor();

    HRESULT hr = CTopLevelWindow3D_StartAnimation_orig(pThis, currentAnimationType);

    auto& animData = g_windowMap[pThis];

    // Windows 10 2004-22H2 doesn't check if the effect group is null so we have to do some bullshit
    if (compositor && !animData.emptyEffect)
    {
        if (HRESULT hr = CCompositor_CreateProxy_CEffectGroupProxy(compositor, &animData.emptyEffect); FAILED(hr))
        {
            Wh_Log(L"Failed to create effect group proxy: %x", hr);
            return hr;
        }
    }

    if (currentAnimationType == WindowAnimationType::AnimationOpen || currentAnimationType == WindowAnimationType::AnimationClose ||
        currentAnimationType == WindowAnimationType::AnimationMinimize || currentAnimationType == WindowAnimationType::AnimationRestoreFromMinimized
    )
    {
        if (compositor && (!animData.matrix || !animData.effect))
        {
            if (hr = CCompositor_CreateProxy_CMatrixTransform3dProxy(compositor, &animData.matrix); FAILED(hr))
            {
                Wh_Log(L"Failed to create matrix transform 3d proxy: %x", hr);
                return hr;
            }
            if (hr = CCompositor_CreateProxy_CEffectGroupProxy(compositor, &animData.effect); FAILED(hr))
            {
                Wh_Log(L"Failed to create effect group proxy: %x", hr);
                return hr;
            }
        }

        if (hr = CVisualProxy_SetEffect(pThis->GetVisualProxy(), animData.effect); FAILED(hr))
        {
            Wh_Log(L"Failed to set effect to visual proxy: %x", hr);
            return hr;
        }

        animData.previousRect = previousRect; // save previous rect for minimize
        animData.currentAnimationType = currentAnimationType;

        if (auto timeline = pThis->GetAnimationTimeline(); timeline)
        {
            switch (currentAnimationType)
            {
                case WindowAnimationType::AnimationOpen:
                case WindowAnimationType::AnimationMinimize:
                case WindowAnimationType::AnimationRestoreFromMinimized:
                    timeline->GetDurationValue() = .25f;
                    break;
                case WindowAnimationType::AnimationClose:
                    timeline->GetDurationValue() = .166f;
                    break;
                default:
                    break;
            }
        }
    }
    else
    {
        if (hr = CVisualProxy_SetEffect(pThis->GetVisualProxy(), animData.emptyEffect); FAILED(hr))
        {
            Wh_Log(L"Failed to set effect to visual proxy: %x", hr);
            return hr;
        }

        return hr;
    }

    return hr;
}

HRESULT (*CTopLevelWindow3D_Destructor_orig)(CTopLevelWindow3D *);
HRESULT CTopLevelWindow3D_Destructor_hook(CTopLevelWindow3D *pThis)
{
	auto it = g_windowMap.find(pThis);

	if (it != g_windowMap.end())
	{
		g_windowMap.erase(it);
	}
    
    return CTopLevelWindow3D_Destructor_orig(pThis);
}

HRESULT (*CTopLevelWindow3D_UpdateAnimatedResources_orig)(CTopLevelWindow3D *);
HRESULT CTopLevelWindow3D_UpdateAnimatedResources_hook(CTopLevelWindow3D *pThis)
{
    auto it = g_windowMap.find(pThis);
    if (it == g_windowMap.end() || !it->second.matrix || !it->second.effect)
    {
        return CTopLevelWindow3D_UpdateAnimatedResources_orig(pThis);
    }
    
    auto data = pThis->GetData();
    if (!data)
    {
        return CTopLevelWindow3D_UpdateAnimatedResources_orig(pThis);
    }

    auto& animData = it->second; 

    auto windowRect = animData.currentAnimationType == WindowAnimationType::AnimationMinimize ?  animData.previousRect : *data->GetWindowRect();
    auto minimizeRect = *pThis->GetMinimizeRect();
    auto timeline = pThis->GetAnimationTimeline();
    float timelineProgress = static_cast<float>(std::max(0.0, std::min(1.0, timeline->GetCurrentValue())));
    float opacity = 0.f;

    auto matrix = BuildMatrix(&opacity, animData.currentAnimationType, timelineProgress, windowRect, minimizeRect);

    if (HRESULT hr = CMatrixTransform3dProxy_Update(animData.matrix, reinterpret_cast<D3DMATRIX*>(&matrix)); SUCCEEDED(hr))
    {
        CEffectGroupProxy_Update(animData.effect, static_cast<double>(opacity), 0, animData.matrix);
    }

    return CTopLevelWindow3D_UpdateAnimatedResources_orig(pThis);
}

HRESULT (*CTopLevelWindow3D_CreateTimelineForTransform_orig)(CTopLevelWindow3D *pThis, UINT eWindowTransformAttribute, float, float, float, float, UINT);
HRESULT CTopLevelWindow3D_CreateTimelineForTransform_hook(CTopLevelWindow3D *pThis, UINT eWindowTransformAttribute, float f1, float f2, float f3, float f4, UINT eInterpolationMode)
{
    WindowAnimationType currentAnimationType = pThis->GetCurrentAnimationType();
    if (currentAnimationType == WindowAnimationType::AnimationOpen || currentAnimationType == WindowAnimationType::AnimationClose ||
        currentAnimationType == WindowAnimationType::AnimationMinimize || currentAnimationType == WindowAnimationType::AnimationRestoreFromMinimized
    )
    {
        return S_OK;
    }
    return CTopLevelWindow3D_CreateTimelineForTransform_orig(pThis, eWindowTransformAttribute, f1, f2, f3, f4, eInterpolationMode);
}

HRESULT (*CAnimatedTransitionVisual_OnStoryboardBegin_orig)(CAnimatedTransitionVisual *, UINT);
HRESULT CAnimatedTransitionVisual_OnStoryboardBegin_hook(CAnimatedTransitionVisual *pThis, UINT uStoryboard)
{
    auto compositor = CDesktopManager::GetInstance()->GetCompositor();

    HRESULT hr = CAnimatedTransitionVisual_OnStoryboardBegin_orig(pThis, uStoryboard);

    if (pThis->GetStoryboardId() != 50) // 50 = Window close
    {
        return hr;
    }

    auto& animData = g_transitionVisualMap[pThis];

    if (compositor && (!animData.matrix || !animData.effect))
    {
        if (hr = CCompositor_CreateProxy_CMatrixTransform3dProxy(compositor, &animData.matrix); FAILED(hr))
        {
            Wh_Log(L"Failed to create matrix transform 3d proxy: %x", hr);
            return hr;
        }
        if (hr = CCompositor_CreateProxy_CEffectGroupProxy(compositor, &animData.effect); FAILED(hr))
        {
            Wh_Log(L"Failed to create effect group proxy: %x", hr);
            return hr;
        }
    }

    animData.startTime = -1.0f;
    UpdateTransitionVisual(pThis, animData);

    return hr;    
}

HRESULT (*CAnimatedTransitionVisual_UpdateGeometry2DAndOpacity_orig)(CAnimatedTransitionVisual *);
HRESULT CAnimatedTransitionVisual_UpdateGeometry2DAndOpacity_hook(CAnimatedTransitionVisual *pThis)
{
    auto it = g_transitionVisualMap.find(pThis);

    if (it != g_transitionVisualMap.end())
    {
        UpdateTransitionVisual(pThis, it->second);
    }

    return CAnimatedTransitionVisual_UpdateGeometry2DAndOpacity_orig(pThis);
}

HRESULT (*CAnimatedTransitionVisual_Scale2D_orig)(CAnimatedTransitionVisual *, const void *);
HRESULT CAnimatedTransitionVisual_Scale2D_hook(CAnimatedTransitionVisual *pThis, const void *pTransform)
{
    if (g_transitionVisualMap.find(pThis) != g_transitionVisualMap.end())
    {
        CVisual_SetDirtyFlags(pThis->GetVisual(), 0x1000);
        return S_OK;
    }

    return CAnimatedTransitionVisual_Scale2D_orig(pThis, pTransform);
}

HRESULT (*CAnimatedTransitionVisual_Opacity_orig)(CAnimatedTransitionVisual *, const TA_TRANSFORM_OPACITY *);
HRESULT CAnimatedTransitionVisual_Opacity_hook(CAnimatedTransitionVisual *pThis, const TA_TRANSFORM_OPACITY *pTransform)
{
    if (g_transitionVisualMap.find(pThis) != g_transitionVisualMap.end())
    {
        TA_TRANSFORM_OPACITY transform = *pTransform;
        transform.rOpacity = 1.0f;
        return CAnimatedTransitionVisual_Opacity_orig(pThis, &transform);
    }

    return CAnimatedTransitionVisual_Opacity_orig(pThis, pTransform);
}

HRESULT (*CAnimatedTransitionVisual_Destructor_orig)(CAnimatedTransitionVisual *);
HRESULT CAnimatedTransitionVisual_Destructor_hook(CAnimatedTransitionVisual *pThis)
{
    auto it = g_transitionVisualMap.find(pThis);

    if (it != g_transitionVisualMap.end())
    {
        g_transitionVisualMap.erase(it);
    }

    return CAnimatedTransitionVisual_Destructor_orig(pThis);
}

BOOL Wh_ModInit() 
{
    Wh_Log(L"Init");

    if (HasDwminitWarningRecently())
    {
        Wh_Log(L"%s", g_kNoInjectDwmMsg);
        return FALSE;
    }

    RTL_OSVERSIONINFOW osVersion;
    RtlGetVersion(&osVersion);
    g_buildNumber = osVersion.dwBuildNumber;

    g_fWindows81 = Wh_GetIntSetting(L"windows81");
    g_fScaleShrink = Wh_GetIntSetting(L"scaleshrink");

    Wh_Log(L"Build number: %d", g_buildNumber);

    const WindhawkUtils::SYMBOL_HOOK udwmDllHooks[] = 
    {
        {
            {
                L"private: long __cdecl CTopLevelWindow3D::StartAnimation(enum CTopLevelWindow3D::WindowAnimationType)",
                L"public: long __cdecl CTopLevelWindow3D::StartAnimation(enum CTopLevelWindow3D::WindowAnimationType)"
            },
            &CTopLevelWindow3D_StartAnimation_orig,
            CTopLevelWindow3D_StartAnimation_hook,
            false
        },
        {
            {
                L"protected: virtual __cdecl CTopLevelWindow3D::~CTopLevelWindow3D(void)"
            },
            &CTopLevelWindow3D_Destructor_orig,
            CTopLevelWindow3D_Destructor_hook,
            false
        },
        {
            {
                L"private: long __cdecl CTopLevelWindow3D::UpdateAnimatedResources(void)"
            },
            &CTopLevelWindow3D_UpdateAnimatedResources_orig,
            CTopLevelWindow3D_UpdateAnimatedResources_hook,
            false
        },
        {
            {
                L"private: long __cdecl CTopLevelWindow3D::CreateTimelineForTransform(enum CTopLevelWindow3D::WindowTransformAttribute,float,float,float,enum InterpolationMode)",
                L"private: long __cdecl CTopLevelWindow3D::CreateTimelineForTransform(enum CTopLevelWindow3D::WindowTransformAttribute,float,float,float,struct InterpolationParameters const &)"
            },
            &CTopLevelWindow3D_CreateTimelineForTransform_orig,
            CTopLevelWindow3D_CreateTimelineForTransform_hook,
            false
        },
        {
            {
                L"public: virtual long __cdecl CAnimatedTransitionVisual::OnStoryboardBegin(unsigned int)"
            },
            &CAnimatedTransitionVisual_OnStoryboardBegin_orig,
            CAnimatedTransitionVisual_OnStoryboardBegin_hook,
            false
        },
        {
            {
                L"protected: long __cdecl CAnimatedTransitionVisual::UpdateGeometry2DAndOpacity(void)"
            },
            &CAnimatedTransitionVisual_UpdateGeometry2DAndOpacity_orig,
            CAnimatedTransitionVisual_UpdateGeometry2DAndOpacity_hook,
            false
        },
        {
            {
                L"protected: virtual __cdecl CAnimatedTransitionVisual::~CAnimatedTransitionVisual(void)"
            },
            &CAnimatedTransitionVisual_Destructor_orig,
            CAnimatedTransitionVisual_Destructor_hook,
            false
        },
        {
            {
                L"public: virtual long __cdecl CAnimatedTransitionVisual::Opacity(struct TA_TRANSFORM_OPACITY const *)"
            },
            &CAnimatedTransitionVisual_Opacity_orig,
            CAnimatedTransitionVisual_Opacity_hook,
            false
        },
        {
            {
                L"public: virtual long __cdecl CAnimatedTransitionVisual::Scale2D(struct TA_TRANSFORM_2D const *)"
            },
            &CAnimatedTransitionVisual_Scale2D_orig,
            CAnimatedTransitionVisual_Scale2D_hook,
            false
        },
        {
            {
                L"protected: long __cdecl CCompositor::CreateProxy<class CMatrixTransform3dProxy>(class CMatrixTransform3dProxy * *)"
            },
            &CCompositor_CreateProxy_CMatrixTransform3dProxy,
            nullptr,
            false
        },
        {
            {
                L"protected: long __cdecl CCompositor::CreateProxy<class CEffectGroupProxy>(class CEffectGroupProxy * *)"
            },
            &CCompositor_CreateProxy_CEffectGroupProxy,
            nullptr,
            false
        },
        {
            {
                L"public: long __cdecl CMatrixTransform3dProxy::Update(struct _D3DMATRIX const &)"
            },
            &CMatrixTransform3dProxy_Update,
            nullptr,
            false,
        },
        {
            {
                L"public: long __cdecl CEffectGroupProxy::Update(double,unsigned int,class CTransform3dGroupProxy const *)"
            },
            &CEffectGroupProxy_Update,
            nullptr,
            false,
        },
        {
            {
                L"public: long __cdecl CVisualProxy::SetEffect(class CResourceProxy *)"
            },
            &CVisualProxy_SetEffect,
            nullptr,
            false,
        },
        {
            {
                L"public: virtual void __cdecl CVisual::SetDirtyFlags(unsigned long)"
            },
            &CVisual_SetDirtyFlags,
            nullptr,
            false
        },
        {
            {
                L"private: static class CDesktopManager * CDesktopManager::s_pDesktopManagerInstance"
            },
            &CDesktopManager::s_pDesktopManagerInstance,
            nullptr,
            false
        }
    };

    if (!WindhawkUtils::HookSymbols(GetModuleHandleW(L"uDWM.dll"), udwmDllHooks, ARRAYSIZE(udwmDllHooks)))
    {
        Wh_Log(L"Failed to hook uDWM.dll");
        return FALSE;
    };

    return TRUE;
}

void Wh_ModSettingsChanged(void)
{
    g_fWindows81 = Wh_GetIntSetting(L"windows81");
    g_fScaleShrink = Wh_GetIntSetting(L"scaleshrink");
}

void Wh_ModUninit() 
{
    Wh_Log(L"Uninit");
    for (auto& [window3d, animData] : g_windowMap) 
    {
        if (animData.emptyEffect)
        {
            CVisualProxy_SetEffect(window3d->GetVisualProxy(), animData.emptyEffect);
        }
    }
}