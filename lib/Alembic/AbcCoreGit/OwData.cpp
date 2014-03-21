//-*****************************************************************************
//
// Copyright (c) 2014,
//
// All rights reserved.
//
//-*****************************************************************************

#include <Alembic/AbcCoreGit/OwData.h>
#include <Alembic/AbcCoreGit/OwImpl.h>
#include <Alembic/AbcCoreGit/CpwData.h>
#include <Alembic/AbcCoreGit/CpwImpl.h>
#include <Alembic/AbcCoreGit/AwImpl.h>
#include <Alembic/AbcCoreGit/WriteUtil.h>
#include <Alembic/AbcCoreGit/Utils.h>
#include <iostream>
#include <string>

namespace Alembic {
namespace AbcCoreGit {
namespace ALEMBIC_VERSION_NS {

//-*****************************************************************************
OwData::OwData( GitGroupPtr iParentGroup,
                const std::string &iName,
                const AbcA::MetaData &iMetaData )
{
	if (iParentGroup)
		TRACE("OwData::OwData(parentGroup:'" << iParentGroup->fullname() << "', name:'" << iName << "')");
	else
		TRACE("OwData::OwData(parentGroup:NULL, '" << iName << "')");

    // Check validity of all inputs.
    ABCA_ASSERT( iParentGroup, "Invalid parent group" );

    // Create the Git group corresponding to this object.
    //m_group.reset( new GitGroup( iParentGroup, iName ) );
    m_group = iParentGroup->addGroup( iName );
    ABCA_ASSERT( m_group,
                 "Could not create group for object: " << iName );
    m_data = Alembic::Util::shared_ptr<CpwData>(
        new CpwData( ".prop", m_group ) );

    AbcA::PropertyHeader topHeader( ".prop", iMetaData );
    UNIMPLEMENTED("WritePropertyInfo()");
#if 0
    WritePropertyInfo( m_group, topHeader, false, 0, 0, 0, 0 );
#endif /* 0 */
}

//-*****************************************************************************
OwData::~OwData()
{
    writeToDisk();

	// not necessary actually
	m_group.reset();
}

//-*****************************************************************************
AbcA::CompoundPropertyWriterPtr
OwData::getProperties( AbcA::ObjectWriterPtr iParent )
{
    AbcA::CompoundPropertyWriterPtr ret = m_top.lock();
    if ( ! ret )
    {
        // time to make a new one
        ret = Alembic::Util::shared_ptr<CpwImpl>( new CpwImpl( iParent,
            m_data, iParent->getMetaData() ) );
        m_top = ret;
    }
    return ret;
}

//-*****************************************************************************
size_t OwData::getNumChildren()
{
    return m_childHeaders.size();
}

//-*****************************************************************************
const AbcA::ObjectHeader & OwData::getChildHeader( size_t i )
{
    if ( i >= m_childHeaders.size() )
    {
        ABCA_THROW( "Out of range index in OwImpl::getChildHeader: "
                     << i );
    }

    ABCA_ASSERT( m_childHeaders[i], "Invalid child header: " << i );

    return *(m_childHeaders[i]);
}

//-*****************************************************************************
const AbcA::ObjectHeader * OwData::getChildHeader( const std::string &iName )
{
    size_t numChildren = m_childHeaders.size();
    for ( size_t i = 0; i < numChildren; ++i )
    {
        if ( m_childHeaders[i]->getName() == iName )
        {
            return m_childHeaders[i].get();
        }
    }

    return NULL;
}

//-*****************************************************************************
AbcA::ObjectWriterPtr OwData::getChild( const std::string &iName )
{
    MadeChildren::iterator fiter = m_madeChildren.find( iName );
    if ( fiter == m_madeChildren.end() )
    {
        return AbcA::ObjectWriterPtr();
    }

    WeakOwPtr wptr = (*fiter).second;
    return wptr.lock();
}

AbcA::ObjectWriterPtr OwData::createChild( AbcA::ObjectWriterPtr iParent,
                                           const std::string & iFullName,
                                           const AbcA::ObjectHeader &iHeader )
{
    std::string name = iHeader.getName();

    if ( m_madeChildren.count( name ) )
    {
        ABCA_THROW( "Already have an Object named: "
                     << name );
    }

    if ( name.empty() )
    {
        ABCA_THROW( "Object not given a name, parent is: " <<
                    iFullName );
    }
    else if ( iHeader.getName().find('/') != std::string::npos )
    {
        ABCA_THROW( "Object has illegal name: "
                     << iHeader.getName() );
    }

    std::string parentName = iFullName;
    if ( parentName != "/" )
    {
        parentName += "/";
    }

    ObjectHeaderPtr header(
        new AbcA::ObjectHeader( iHeader.getName(),
                                parentName + iHeader.getName(),
                                iHeader.getMetaData() ) );

    Alembic::Util::shared_ptr<OwImpl> ret( new OwImpl( iParent,
                            //m_group,
                            m_group->addGroup( iHeader.getName() ),
                            header, m_childHeaders.size() ) );

    m_childHeaders.push_back( header );
    m_madeChildren[iHeader.getName()] = WeakOwPtr( ret );

    m_hashes.push_back(0);
    m_hashes.push_back(0);

    return ret;
}

//-*****************************************************************************
void OwData::writeHeaders( MetaDataMapPtr iMetaDataMap,
                           Util::SpookyHash & ioHash )
{
    std::vector< Util::uint8_t > data;

    // pack all object header into data here
    for ( size_t i = 0; i < m_childHeaders.size(); ++i )
    {
        WriteObjectHeader( data, *m_childHeaders[i], iMetaDataMap );
    }

    Util::SpookyHash dataHash;
    dataHash.Init( 0, 0 );
    m_data->computeHash( dataHash );

    Util::uint64_t hashes[4];
    dataHash.Final( &hashes[0], &hashes[1] );

    ioHash.Init( 0, 0 );

    if ( !m_hashes.empty() )
    {
        ioHash.Update( &m_hashes.front(), m_hashes.size() * 8 );
        ioHash.Final( &hashes[2], &hashes[3] );
    }
    else
    {
        hashes[2] = 0;
        hashes[3] = 0;
    }

    // add the  data hash and child hash for writing
    Util::uint8_t * hashData = ( Util::uint8_t * ) hashes;
    for ( size_t i = 0; i < 32; ++i )
    {
        data.push_back( hashData[i] );
    }

    // now update childHash with dataHash
    // SpookyHash has the nice property that Final doesn't invalidate the hash
    ioHash.Update( hashes, 16 );

    if ( !data.empty() )
    {
        m_group->addData( data.size(), &( data.front() ) );
    }

    m_data->writePropertyHeaders( iMetaDataMap );
}

void OwData::fillHash( std::size_t iIndex, Util::uint64_t iHash0,
                       Util::uint64_t iHash1 )
{
    ABCA_ASSERT( iIndex < m_childHeaders.size() &&
                 iIndex * 2 < m_hashes.size(),
                 "Invalid property index requested in OwData::fillHash" );

    m_hashes[ iIndex * 2     ] = iHash0;
    m_hashes[ iIndex * 2 + 1 ] = iHash1;
}

void OwData::writeToDisk()
{
    TRACE("OwData::writeToDisk() path:'" << absPathname() << "'");
    ABCA_ASSERT( m_group, "invalid group" );
    m_group->writeToDisk();
}


} // End namespace ALEMBIC_VERSION_NS
} // End namespace AbcCoreGit
} // End namespace Alembic