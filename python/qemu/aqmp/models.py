"""
QMP Data Models

This module provides simplistic data classes that represent the few
structures that the QMP spec mandates; they are used to verify incoming
data to make sure it conforms to spec.
"""
# pylint: disable=too-few-public-methods

from collections import abc
import copy
from typing import (
    Any,
    Dict,
    Mapping,
    Optional,
    Sequence,
)


class Model:
    """
    Abstract data model, representing some QMP object of some kind.

    :param raw: The raw object to be validated.
    :raise KeyError: If any required fields are absent.
    :raise TypeError: If any required fields have the wrong type.
    """
    def __init__(self, raw: Mapping[str, Any]):
        self._raw = raw

    def _check_key(self, key: str) -> None:
        if key not in self._raw:
            raise KeyError(f"'{self._name}' object requires '{key}' member")

    def _check_value(self, key: str, type_: type, typestr: str) -> None:
        assert key in self._raw
        if not isinstance(self._raw[key], type_):
            raise TypeError(
                f"'{self._name}' member '{key}' must be a {typestr}"
            )

    def _check_member(self, key: str, type_: type, typestr: str) -> None:
        self._check_key(key)
        self._check_value(key, type_, typestr)

    @property
    def _name(self) -> str:
        return type(self).__name__

    def __repr__(self) -> str:
        return f"{self._name}({self._raw!r})"


class Greeting(Model):
    """
    Defined in qmp-spec.txt, section 2.2, "Server Greeting".

    :param raw: The raw Greeting object.
    :raise KeyError: If any required fields are absent.
    :raise TypeError: If any required fields have the wrong type.
    """
    def __init__(self, raw: Mapping[str, Any]):
        super().__init__(raw)
        #: 'QMP' member
        self.QMP: QMPGreeting  # pylint: disable=invalid-name

        self._check_member('QMP', abc.Mapping, "JSON object")
        self.QMP = QMPGreeting(self._raw['QMP'])

    def _asdict(self) -> Dict[str, object]:
        """
        For compatibility with the iotests sync QMP wrapper.

        The legacy QMP interface needs Greetings as a garden-variety Dict.

        This interface is private in the hopes that it will be able to
        be dropped again in the near-future. Caller beware!
        """
        return dict(copy.deepcopy(self._raw))


class QMPGreeting(Model):
    """
    Defined in qmp-spec.txt, section 2.2, "Server Greeting".

    :param raw: The raw QMPGreeting object.
    :raise KeyError: If any required fields are absent.
    :raise TypeError: If any required fields have the wrong type.
    """
    def __init__(self, raw: Mapping[str, Any]):
        super().__init__(raw)
        #: 'version' member
        self.version: Mapping[str, object]
        #: 'capabilities' member
        self.capabilities: Sequence[object]

        self._check_member('version', abc.Mapping, "JSON object")
        self.version = self._raw['version']

        self._check_member('capabilities', abc.Sequence, "JSON array")
        self.capabilities = self._raw['capabilities']


class ErrorResponse(Model):
    """
    Defined in qmp-spec.txt, section 2.4.2, "error".

    :param raw: The raw ErrorResponse object.
    :raise KeyError: If any required fields are absent.
    :raise TypeError: If any required fields have the wrong type.
    """
    def __init__(self, raw: Mapping[str, Any]):
        super().__init__(raw)
        #: 'error' member
        self.error: ErrorInfo
        #: 'id' member
        self.id: Optional[object] = None  # pylint: disable=invalid-name

        self._check_member('error', abc.Mapping, "JSON object")
        self.error = ErrorInfo(self._raw['error'])

        if 'id' in raw:
            self.id = raw['id']


class ErrorInfo(Model):
    """
    Defined in qmp-spec.txt, section 2.4.2, "error".

    :param raw: The raw ErrorInfo object.
    :raise KeyError: If any required fields are absent.
    :raise TypeError: If any required fields have the wrong type.
    """
    def __init__(self, raw: Mapping[str, Any]):
        super().__init__(raw)
        #: 'class' member, with an underscore to avoid conflicts in Python.
        self.class_: str
        #: 'desc' member
        self.desc: str

        self._check_member('class', str, "string")
        self.class_ = self._raw['class']

        self._check_member('desc', str, "string")
        self.desc = self._raw['desc']
