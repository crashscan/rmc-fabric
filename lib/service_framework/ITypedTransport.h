#pragma once

#include "QueryServiceConcept.h"

namespace RSCGroup {

/**
 * @brief Concept-constrained mixin that adds typed query service binding.
 *
 * Template parameter QS must satisfy the QueryService concept (i.e. must
 * implement bool isReady()).  The compile-time constraint ensures that only
 * compatible service types can be bound; the compiler rejects any
 * non-conforming type immediately with a clear diagnostic.
 *
 * Binding must happen before start().  Concrete transports implement
 * bindQueryService() to store the reference and forward D-Bus (or other
 * protocol) method calls to it.
 *
 * This class is a pure mixin: it adds only bindQueryService() and does not
 * constrain start()/stop() return types, allowing service-specific transport
 * interfaces (e.g. inventory's ITransport) to inherit it without signature
 * conflicts.
 */
template <QueryService QS>
class ITypedTransport {
public:
    virtual ~ITypedTransport() = default;

    /**
     * @brief Bind a query service provider.
     *
     * Must be called before start().
     */
    virtual void bindQueryService(QS& service) = 0;
};

} // namespace RSCGroup
