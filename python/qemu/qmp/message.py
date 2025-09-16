"""
QMP Message Format

This module provides the `Message` class, which represents a single QMP
message sent to or from the server.
"""

import json
from json import JSONDecodeError
from typing import (
    Dict,
    Iterator,
    Mapping,
    MutableMapping,
    Optional,
    Union,
)

from .error import ProtocolError


class Message(MutableMapping[str, object]):
    """
    Represents a single QMP protocol message.

    QMP uses JSON objects as its basic communicative unit; so this
    Python object is a :py:obj:`~collections.abc.MutableMapping`. It may
    be instantiated from either another mapping (like a `dict`), or from
    raw `bytes` that still need to be deserialized.

    Once instantiated, it may be treated like any other
    :py:obj:`~collections.abc.MutableMapping`::

        >>> msg = Message(b'{"hello": "world"}')
        >>> assert msg['hello'] == 'world'
        >>> msg['id'] = 'foobar'
        >>> print(msg)
        {
          "hello": "world",
          "id": "foobar"
        }

    It can be converted to `bytes`::

        >>> msg = Message({"hello": "world"})
        >>> print(bytes(msg))
        b'{"hello":"world","id":"foobar"}'

    Or back into a garden-variety `dict`::

       >>> dict(msg)
       {'hello': 'world'}

    Or pretty-printed::

       >>> print(str(msg))
       {
         "hello": "world"
       }

    :param value: Initial value, if any.
    :param eager:
        When `True`, attempt to serialize or deserialize the initial value
        immediately, so that conversion exceptions are raised during
        the call to ``__init__()``.

    """
    # pylint: disable=too-many-ancestors

    def __init__(self,
                 value: Union[bytes, Mapping[str, object]] = b'{}', *,
                 eager: bool = True):
        self._data: Optional[bytes] = None
        self._obj: Optional[Dict[str, object]] = None

        if isinstance(value, bytes):
            self._data = value
            if eager:
                self._obj = self._deserialize(self._data)
        else:
            self._obj = dict(value)
            if eager:
                self._data = self._serialize(self._obj)

    # Methods necessary to implement the MutableMapping interface, see:
    # https://docs.python.org/3/library/collections.abc.html#collections.abc.MutableMapping

    # We get pop, popitem, clear, update, setdefault, __contains__,
    # keys, items, values, get, __eq__ and __ne__ for free.

    def __getitem__(self, key: str) -> object:
        return self._object[key]

    def __setitem__(self, key: str, value: object) -> None:
        self._object[key] = value
        self._data = None

    def __delitem__(self, key: str) -> None:
        del self._object[key]
        self._data = None

    def __iter__(self) -> Iterator[str]:
        return iter(self._object)

    def __len__(self) -> int:
        return len(self._object)

    # Dunder methods not related to MutableMapping:

    def __repr__(self) -> str:
        if self._obj is not None:
            return f"Message({self._object!r})"
        return f"Message({bytes(self)!r})"

    def __str__(self) -> str:
        """Pretty-printed representation of this QMP message."""
        return json.dumps(self._object, indent=2)

    def __bytes__(self) -> bytes:
        """bytes representing this QMP message."""
        if self._data is None:
            self._data = self._serialize(self._obj or {})
        return self._data

    # Conversion Methods

    @property
    def _object(self) -> Dict[str, object]:
        """
        A `dict` representing this QMP message.

        Generated on-demand, if required. This property is private
        because it returns an object that could be used to invalidate
        the internal state of the `Message` object.
        """
        if self._obj is None:
            self._obj = self._deserialize(self._data or b'{}')
        return self._obj

    @classmethod
    def _serialize(cls, value: object) -> bytes:
        """
        Serialize a JSON object as `bytes`.

        :raise ValueError: When the object cannot be serialized.
        :raise TypeError: When the object cannot be serialized.

        :return: `bytes` ready to be sent over the wire.
        """
        return json.dumps(value, separators=(',', ':')).encode('utf-8')

    @classmethod
    def _deserialize(cls, data: bytes) -> Dict[str, object]:
        """
        Deserialize JSON `bytes` into a native Python `dict`.

        :raise DeserializationError:
            If JSON deserialization fails for any reason.
        :raise UnexpectedTypeError:
            If the data does not represent a JSON object.

        :return: A `dict` representing this QMP message.
        """
        try:
            obj = json.loads(data)
        except JSONDecodeError as err:
            emsg = "Failed to deserialize QMP message."
            raise DeserializationError(emsg, data) from err
        if not isinstance(obj, dict):
            raise UnexpectedTypeError(
                "QMP message is not a JSON object.",
                obj
            )
        return obj


class DeserializationError(ProtocolError):
    """
    A QMP message was not understood as JSON.

    When this Exception is raised, ``__cause__`` will be set to the
    `json.JSONDecodeError` Exception, which can be interrogated for
    further details.

    :param error_message: Human-readable string describing the error.
    :param raw: The raw `bytes` that prompted the failure.
    """
    def __init__(self, error_message: str, raw: bytes):
        super().__init__(error_message, raw)
        #: The raw `bytes` that were not understood as JSON.
        self.raw: bytes = raw

    def __str__(self) -> str:
        return "\n".join((
            super().__str__(),
            f"  raw bytes were: {str(self.raw)}",
        ))


class UnexpectedTypeError(ProtocolError):
    """
    A QMP message was JSON, but not a JSON object.

    :param error_message: Human-readable string describing the error.
    :param value: The deserialized JSON value that wasn't an object.
    """
    def __init__(self, error_message: str, value: object):
        super().__init__(error_message, value)
        #: The JSON value that was expected to be an object.
        self.value: object = value

    def __str__(self) -> str:
        strval = json.dumps(self.value, indent=2)
        return "\n".join((
            super().__str__(),
            f"  json value was: {strval}",
        ))
