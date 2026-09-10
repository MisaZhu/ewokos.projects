#ifndef PATHINTERFACE_HPP
#define PATHINTERFACE_HPP

#include "src/defines.hpp"

class Tile;
class TargetedPath;

/**
 * @brief An interface representing a path that can be followed by a charater.
 */
class PathInterface
{
    public:
        virtual ~PathInterface() {}

        // Returns this as a TargetedPath, or nullptr when the path is not one
        // (e.i. a RandomRoadPath).  Replaces QSharedPointer::dynamicCast<
        // TargetedPath>() in MotionHandler.cpp, which expands to a
        // dynamic_cast that EwokOS builds without (-fno-rtti).  A single
        // virtual rather than a type enum because TargetedPath is the only
        // subtype ever tested for.
        virtual TargetedPath* asTargetedPath() { return nullptr; }

        /**
         * @brief Indicate if the path is obsolete.
         *
         * An obsolete path can occur when some of it's tiles are occupied (e.i. constructions).
         */
        virtual bool isObsolete() const = 0;
        virtual bool isCompleted() const = 0;

        virtual bool isNextTileValid() const = 0;
        virtual const Tile& getNextTile() = 0;
};

#endif // PATHINTERFACE_HPP
