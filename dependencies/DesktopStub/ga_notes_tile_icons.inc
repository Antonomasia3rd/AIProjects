// Original bundled MIT icons, decoded only from compiled, bounded resources.
#include "../third_party/RealTimeNotesAssets/icons.h"
static std::unique_ptr<Bitmap> LoadNotesTileIcon(const std::wstring& topic)
{
    const auto* icon = aip::notes::assets::Find(topic);
    if (!icon || icon->size > 65536) return {};
    ComApartmentScope apartment;
    if (!apartment.ok()) return {};
    UniqueComPtr<IStream> stream(SHCreateMemStream(icon->bytes, static_cast<UINT>(icon->size)));
    if (!stream) return {};
    IWICImagingFactory* rawFactory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&rawFactory)))) return {};
    UniqueComPtr<IWICImagingFactory> factory(rawFactory);
    IWICBitmapDecoder* rawDecoder = nullptr;
    if (FAILED(factory->CreateDecoderFromStream(stream.get(), nullptr, WICDecodeMetadataCacheOnLoad, &rawDecoder))) return {};
    UniqueComPtr<IWICBitmapDecoder> decoder(rawDecoder);
    IWICBitmapFrameDecode* rawFrame = nullptr;
    if (FAILED(decoder->GetFrame(0, &rawFrame))) return {};
    UniqueComPtr<IWICBitmapFrameDecode> frame(rawFrame);
    UINT width = 0, height = 0;
    if (FAILED(frame->GetSize(&width, &height)) || !width || !height || width > 128 || height > 128) return {};
    IWICFormatConverter* rawConverter = nullptr;
    if (FAILED(factory->CreateFormatConverter(&rawConverter))) return {};
    UniqueComPtr<IWICFormatConverter> converter(rawConverter);
    if (FAILED(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    const UINT stride = width * 4;
    std::vector<BYTE> pixels(stride * height);
    if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data()))) return {};
    auto bitmap = std::make_unique<Bitmap>(static_cast<INT>(width), static_cast<INT>(height), PixelFormat32bppARGB);
    if (!bitmap || bitmap->GetLastStatus() != Ok) return {};
    Rect area(0, 0, static_cast<INT>(width), static_cast<INT>(height)); BitmapData data{};
    if (bitmap->LockBits(&area, ImageLockModeWrite, PixelFormat32bppARGB, &data) != Ok) return {};
    for (UINT row = 0; row < height; ++row)
        memcpy(static_cast<BYTE*>(data.Scan0) + static_cast<std::ptrdiff_t>(row) * data.Stride, pixels.data() + row * stride, stride);
    if (bitmap->UnlockBits(&data) != Ok) return {};
    return bitmap;
}
