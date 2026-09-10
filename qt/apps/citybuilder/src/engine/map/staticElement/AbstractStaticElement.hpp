#ifndef ABSTRACTSTATICELEMENT_HPP
#define ABSTRACTSTATICELEMENT_HPP

#include <QtCore/QtGlobal>

class NatureElement;

/**
 * @brief An empty base class for all static elements.
 */
class AbstractStaticElement
{
        Q_DISABLE_COPY_MOVE(AbstractStaticElement)

    public:
        AbstractStaticElement() {};
        virtual ~AbstractStaticElement() {};

        // Returns this as a NatureElement, or nullptr when the element is not
        // one (e.i. a building or a road).  Replaces QSharedPointer::
        // dynamicCast<NatureElement>() in MinerCharacter.cpp, which expands to
        // a dynamic_cast that EwokOS builds without (-fno-rtti).  A single
        // virtual rather than a type enum because NatureElement is the only
        // subtype ever tested for.
        virtual NatureElement* asNatureElement() { return nullptr; }
};

#endif // ABSTRACTSTATICELEMENT_HPP
