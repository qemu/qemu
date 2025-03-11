"""
QAPI domain extension.
"""

from __future__ import annotations

from typing import (
    TYPE_CHECKING,
    AbstractSet,
    Any,
    Dict,
    Tuple,
)

from sphinx.domains import Domain, ObjType
from sphinx.util import logging


if TYPE_CHECKING:
    from sphinx.application import Sphinx

logger = logging.getLogger(__name__)


class QAPIDomain(Domain):
    """QAPI language domain."""

    name = "qapi"
    label = "QAPI"

    object_types: Dict[str, ObjType] = {}
    directives = {}
    roles = {}
    initial_data: Dict[str, Dict[str, Tuple[Any]]] = {}
    indices = []

    def merge_domaindata(
        self, docnames: AbstractSet[str], otherdata: Dict[str, Any]
    ) -> None:
        pass

    def resolve_any_xref(self, *args: Any, **kwargs: Any) -> Any:
        # pylint: disable=unused-argument
        return []


def setup(app: Sphinx) -> Dict[str, Any]:
    app.setup_extension("sphinx.directives")
    app.add_domain(QAPIDomain)

    return {
        "version": "1.0",
        "env_version": 1,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
