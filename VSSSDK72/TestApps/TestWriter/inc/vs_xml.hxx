/*++

Copyright (c) 1999  Microsoft Corporation

Module Name:

    vs_xml.hxx

Abstract:

    Declaration of XML wrapper classes


    Adi Oltean  [aoltean]  11/17/1999

TBD:
	
	Add comments.

Revision History:

    Name        Date        Comments
    aoltean     11/17/1999  Created
	brianb		03/13/2000  Added XML support for Backup Extensions

--*/

#ifndef __VSS_XML_HXX__
#define __VSS_XML_HXX__

#if _MSC_VER > 1000
#pragma once
#endif

////////////////////////////////////////////////////////////////////////
//  Standard foo for file name aliasing.  This code block must be after
//  all includes of VSS header files.
//
#ifdef VSS_FILE_ALIAS
#undef VSS_FILE_ALIAS
#endif
#define VSS_FILE_ALIAS "INCXMLH"
//
////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////
// Forward declarations


class CXMLDocument;
class CXMLNode;



/////////////////////////////////////////////////////////////////////////////
// CXMLNode


class CXMLNode
{
// Constructors& destructors
public:

	// null constructor
	CXMLNode() {};

	// constructor wher toplevel document node is passed in
	CXMLNode(const CXMLNode& node):
	  m_pDoc(node.m_pDoc), m_pNode(node.m_pNode) {};

	// constructor where both node and document are passed in
	CXMLNode(IXMLDOMNode* pNode, IXMLDOMDocument* pDoc):
	  m_pDoc(pDoc), m_pNode(pNode) {};

	// assignment operator
	void operator = (CXMLNode other)
	{
	    m_pDoc = other.GetDocument();
	    m_pNode = other.GetNodeInterface();
	}


// Attributes
public:

	bool IsEmpty() const { return (m_pNode == NULL); };

// Methods
public:
	// insert a node under another node
	IXMLDOMNode* InsertChild
		(
		IN	IXMLDOMNode* pChildNode,
		IN  const CComVariant& vAfter = CComVariant()
		) throw(HRESULT);

	// append a node after a particular child node
	void AppendChild
		(
		IN	CXMLNode& childNode,
		OUT	IXMLDOMNode** ppNewChildNode = NULL
		) throw(HRESULT);

    // set an attribute to a GUID value
	void SetAttribute
		(
		IN  LPCWSTR wszAttributeName,
		IN  GUID ValueId
		) throw(HRESULT);

	// set value of a byte array attribute by UUENCODING the data
	void SetAttribute
		(
		LPCWSTR wszAttr,
		const BYTE *pbVal,
		UINT cbVal
		);

	// set the value of an attribute to an ASCII string
	void SetAttribute
		(
		IN LPCWSTR wszAttrName,
		IN LPCSTR szValue
		);

    // set an attribute to a string value
	void SetAttribute
		(
		IN  LPCWSTR wszAttributeName,
		IN  LPCWSTR wszValue
		) throw(HRESULT);

    // set an attribute to an integer value
	void SetAttribute
		(
		IN  LPCWSTR wszAttributeName,
		IN  INT nValue
		) throw(HRESULT);

    // set an attribute to a DWORD value
	void SetAttribute
		(
		IN  LPCWSTR wszAttributeName,
		IN  DWORD dwValue
		) throw(HRESULT);

    // set an attribute to a LONGLONG value
	void SetAttribute
		(
		IN  LPCWSTR wszAttributeName,
		IN  LONGLONG llValue
		) throw(HRESULT);

    // set the text value of a node
	void SetValue
		(
		IN  LPCWSTR wszValue
		) throw(HRESULT);

    // add text to a node
	void AddText
		(
		IN  LPCWSTR wszText
		);

    IXMLDOMDocument *GetDocument()
		{
		return m_pDoc;
		}

    IXMLDOMNode *GetNodeInterface()
		{
		return m_pNode;
		}

    // save the node as an XML string
    BSTR SaveAsXML() throw(HRESULT);

    // insert a node as a child of the current node
    CXMLNode InsertNode
		(
		CXMLNode &node
		) throw(HRESULT);


// Data members
protected:

	// toplevel document
    CComPtr<IXMLDOMDocument> m_pDoc;

	// node
    CComPtr<IXMLDOMNode> m_pNode;

};



/////////////////////////////////////////////////////////////////////////////
// CXMLDocument


class CXMLDocument: public CXMLNode
{
private:
	// node currently positioned on
	CComPtr<IXMLDOMNode> m_pNodeCur;

	// attribute map for node currently positioned on
	CComPtr<IXMLDOMNamedNodeMap> m_pAttributeMap;

	// level from root (0)
	unsigned m_level;

// Constructors& destructors
public:
	// constructor where toplevel document node is passed in
	CXMLDocument(IXMLDOMDocument* pDoc = NULL):
		  m_pNodeCur(pDoc),
		  m_pAttributeMap(NULL),
		  CXMLNode(pDoc, pDoc),
		  m_level(0)
		  {
		  }

	// constructor where both toplevel node and toplevel document
	// are passed in
    CXMLDocument(IXMLDOMNode *pNode, IXMLDOMDocument *pDoc) :
		m_pNodeCur(pNode),
		m_level(0),
		m_pAttributeMap(NULL),
		CXMLNode(pNode, pDoc)
		{
		}


	// copy constructor
	CXMLDocument(const CXMLDocument& doc):
		CXMLNode(doc),
		m_pNodeCur(doc.m_pNodeCur),
		m_level(doc.m_level)
		{
		}

    // convert a node into a document
	CXMLDocument(const CXMLNode& node) :
		CXMLNode(node),
		m_level(0)
		{
		m_pNodeCur = m_pNode;
		}

// Methods
public:

	// return interface to toplevel document
	IXMLDOMDocument* GetInterface() const { return m_pDoc; };

	// set toplevel node to be the current node
	void SetToplevel()
		{
		m_level = 0;
		m_pNode = m_pNodeCur;
		}

	// set a particular node as the toplevel node in the document
	void SetToplevelNode(CXMLNode &node)
		{
		m_level = 0;
		m_pNode = node.GetNodeInterface();
		m_pNodeCur = m_pNode;
		}

	// initialize the document
	void Initialize() throw(HRESULT);

    // create a node within the document
	CXMLNode CreateNode
		(
		IN	LPCWSTR wszName,
		IN	DOMNodeType nType = NODE_ELEMENT
		) throw(HRESULT);

    // reset current position to toplevel node
    void ResetToDocument
		(
		);

    // reset position to the parent node
	void ResetToParent
		(
		) throw(HRESULT);

    // move to next node within the document
	bool Next
		(
		IN bool fDescend = TRUE,
		IN bool fAscendAllowed = TRUE
		) throw(HRESULT);

    // find a particular attribute of the current node
    bool FindAttribute
		(
		IN LPCWSTR wszAttrName,
		OUT BSTR *pbstrAttrValue
		) throw(HRESULT);

    // position to the next attribute in the current node
    IXMLDOMNode *NextAttribute
		(
		);

    // find a particular sibling or child element
	bool FindElement
		(
		IN LPCWSTR wsz,
		IN bool bGotoChild
		);

    bool FindElementOneOf
            (
            IN LPCWSTR wsz[],
            IN LONG lArraySize,
            IN bool bGotoChild
            );
    
    // load the document from an XML string
    bool LoadFromXML
    	(
		IN BSTR bstrXML
		) throw(HRESULT);

    // load the document from a file (currently only
	// used for testing purposes)
    bool LoadFromFile
		(
		IN LPCWSTR wszFile
		) throw(HRESULT);

    // return the curernt node
    inline IXMLDOMNode *GetCurrentNode()
		{
		return m_pNodeCur;
		}


	inline SetCurrentNode(IXMLDOMNode *pNode)
		{
		m_pNodeCur = pNode;
		}

	// return the current level from the toplevel node
	inline unsigned GetLevel() { return m_level; }

private:
	// is the node an element with a specific element type
	bool IsNodeMatch
		(
		LPCWSTR wszElementType
		);
	};


#endif // __VSS_XML_HXX__
