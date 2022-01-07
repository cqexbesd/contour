/**
 * This file is part of the "libterminal" project
 *   Copyright (c) 2019-2020 Christian Parpart <christian@parpart.family>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include <terminal/Image.h>

#include <algorithm>
#include <memory>

using std::copy;
using std::make_shared;
using std::min;
using std::move;
using std::shared_ptr;

using crispy::LRUCapacity;
using crispy::StrongHashtableSize;

namespace terminal
{

ImageStats& ImageStats::get()
{
    static ImageStats stats {};
    return stats;
}

Image::~Image()
{
    --ImageStats::get().instances;
    onImageRemove_(this);
}

RasterizedImage::~RasterizedImage()
{
    --ImageStats::get().rasterized;
}

ImageFragment::~ImageFragment()
{
    --ImageStats::get().fragments;
}

ImagePool::ImagePool(OnImageRemove _onImageRemove, ImageId _nextImageId):
    nextImageId_ { _nextImageId },
    imageNameToImageCache_ { StrongHashtableSize { 1024 }, LRUCapacity { 100 } },
    onImageRemove_ { std::move(_onImageRemove) }
{
}

Image::Data RasterizedImage::fragment(Coordinate _pos) const
{
    // TODO: respect alignment hint
    // TODO: respect resize hint

    auto const xOffset = _pos.column * unbox<int>(cellSize_.width);
    auto const yOffset = _pos.line * unbox<int>(cellSize_.height);
    auto const pixelOffset = Coordinate { yOffset, xOffset };

    Image::Data fragData;
    fragData.resize(*cellSize_.width * *cellSize_.height * 4); // RGBA
    auto const availableWidth =
        min(unbox<int>(image_->width()) - *pixelOffset.column, unbox<int>(cellSize_.width));
    auto const availableHeight =
        min(unbox<int>(image_->height()) - *pixelOffset.line, unbox<int>(cellSize_.height));

    // auto const availableSize = Size{availableWidth, availableHeight};
    // std::cout << fmt::format(
    //     "RasterizedImage.fragment({}): pixelOffset={}, cellSize={}/{}\n",
    //     _pos,
    //     pixelOffset,
    //     cellSize_,
    //     availableSize
    // );

    // auto const fitsWidth = pixelOffset.column + cellSize_.width < image_.get().width();
    // auto const fitsHeight = pixelOffset.line + cellSize_.height < image_.get().height();
    // if (!fitsWidth || !fitsHeight)
    //     std::cout << fmt::format("ImageFragment: out of bounds{}{} ({}x{}); {}\n",
    //             fitsWidth ? "" : " (width)",
    //             fitsHeight ? "" : " (height)",
    //             availableWidth,
    //             availableHeight,
    //             *this);

    // TODO: if input format is (RGB | PNG), transform to RGBA

    auto target = &fragData[0];

    // fill horizontal gap at the bottom
    for (int y = availableHeight * unbox<int>(cellSize_.width); y < *cellSize_.height * *cellSize_.width; ++y)
    {
        *target++ = defaultColor_.red();
        *target++ = defaultColor_.green();
        *target++ = defaultColor_.blue();
        *target++ = defaultColor_.alpha();
    }

    for (int y = 0; y < availableHeight; ++y)
    {
        auto const startOffset =
            ((*pixelOffset.line + (availableHeight - 1 - y)) * *image_->width() + *pixelOffset.column) * 4;
        auto const source = &image_->data()[startOffset];
        target = copy(source, source + availableWidth * 4, target);

        // fill vertical gap on right
        for (int x = availableWidth; x < *cellSize_.width; ++x)
        {
            *target++ = defaultColor_.red();
            *target++ = defaultColor_.green();
            *target++ = defaultColor_.blue();
            *target++ = defaultColor_.alpha();
        }
    }

    return fragData;
}

shared_ptr<Image const> ImagePool::create(ImageFormat _format, ImageSize _size, Image::Data&& _data)
{
    // TODO: This operation should be idempotent, i.e. if that image has been created already, return a
    // reference to that.
    auto const id = nextImageId_++;
    return make_shared<Image>(id, _format, move(_data), _size, onImageRemove_);
}

shared_ptr<RasterizedImage> ImagePool::rasterize(shared_ptr<Image const> _image,
                                                 ImageAlignment _alignmentPolicy,
                                                 ImageResize _resizePolicy,
                                                 RGBAColor _defaultColor,
                                                 GridSize _cellSpan,
                                                 ImageSize _cellSize)
{
    return make_shared<RasterizedImage>(
        std::move(_image), _alignmentPolicy, _resizePolicy, _defaultColor, _cellSpan, _cellSize);
}

void ImagePool::link(std::string const& _name, shared_ptr<Image const> _imageRef)
{
    imageNameToImageCache_.emplace(_name, std::move(_imageRef));
}

shared_ptr<Image const> ImagePool::findImageByName(std::string const& _name) const noexcept
{
    if (auto imageRef = imageNameToImageCache_.try_get(_name))
        return *imageRef;

    return {};
}

void ImagePool::unlink(std::string const& _name)
{
    imageNameToImageCache_.erase(_name);
}

void ImagePool::clear()
{
    imageNameToImageCache_.clear();
}

void ImagePool::inspect(std::ostream& os) const
{
    os << "image name links:\n";
    imageNameToImageCache_.inspect(os);
    os << fmt::format("global image stats: {}\n", ImageStats::get());
}

} // namespace terminal
